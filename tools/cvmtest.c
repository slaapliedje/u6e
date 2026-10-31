// SPDX-License-Identifier: GPL-2.0-or-later
/* cvmtest.c — HOST unit test for the conversation VM's data-block lookup
 * (INDEXOF / db_find). Includes the engine TU directly to reach its statics,
 * builds a known data block, and checks name->index resolution.
 */
#include <stdio.h>
#include <string.h>
#include "converse_vm.c"

int main(void)
{
    static cvm_game_t g; cvm_game_init(&g, 1);
    static cvm_t vm; vm.g = &g;
    /* a data block of item names (non-print 0x00 terminators), ENDDATA at end */
    static const uint8_t blk[] = {
        'B','o','w',0, 'C','r','o','s','s','b','o','w',0, 'S','l','i','n','g',0,
        'M','a','g','i','c',' ','B','o','w',0, 0xb8 };
    vm.s = blk; vm.len = (int32_t)sizeof blk;

    struct { const char *q; uint32_t exp; } t[] = {
        {"Bow",0}, {"Crossbow",1}, {"Sling",2}, {"Magic Bow",3}, {"Sword",4} /* 4 = not found (count) */
    };
    int fail = 0;
    for (unsigned i = 0; i < sizeof t/sizeof*t; i++) {
        uint32_t r = db_find(&vm, 0, t[i].q);
        int ok = (r == t[i].exp);
        printf("  db_find(\"%s\") = %u (expect %u) %s\n", t[i].q, r, t[i].exp, ok?"OK":"FAIL");
        if (!ok) fail++;
    }
    printf(fail ? "\nINDEXOF/db_find: %d FAILURE(S)\n" : "\nINDEXOF/db_find: ALL PASS\n", fail);

    /* system numeric vars: read game state through the VAR index space */
    printf("\nsystem numeric vars (VAR/sysvar):\n");
    g.karma=42; g.avatar_sex=1; g.on_quest=1; g.knows_gargish=1; g.avatar_hp=27;
    g.in_party[62]=1; g.in_party[3]=1; g.dead[3]=1;   /* 2 members: the reference sets PARTYLIVE and PARTYALL to the size MINUS the leader (usecode_vm.md §6c) */
    vm.npc=5; g.worktype[5]=7;
    struct { uint32_t idx; int32_t exp; const char*nm; } s[] = {
        {0x14,42,"karma"}, {0x10,1,"sex"}, {0x15,1,"gargish"}, {0x19,27,"hp"},
        {0x1A,1,"quest"}, {0x18,1,"party-all"}, {0x17,1,"party-live"},
        {0x20,7,"worktype"}, {0x03,-1,"local(not sysvar)"},
    };
    int sf=0;
    for (unsigned i=0;i<sizeof s/sizeof*s;i++){
        int32_t v=-1; int is=sysvar(&vm,s[i].idx,&v);
        int32_t got = is ? v : -1;
        int ok = (got==s[i].exp);
        printf("  var[0x%02x] %-18s = %d (expect %d) %s\n", s[i].idx, s[i].nm, got, s[i].exp, ok?"OK":"FAIL");
        if(!ok){ sf++; fail++; }
    }
    printf(sf ? "system vars: %d FAILURE(S)\n" : "system vars: ALL PASS\n", sf);

    /* $T time-of-day token: print_text() resolves "$T" to the greeting word by g.hour. */
    printf("\n$T time-of-day token:\n");
    vm.player="Avatar"; vm.gtitle="milord"; vm.npcname[0]=0;
    struct { int hour; const char *exp; } tt[] = {
        {0,"morning"}, {9,"morning"}, {11,"morning"},
        {12,"afternoon"}, {15,"afternoon"}, {17,"afternoon"},
        {18,"evening"}, {21,"evening"}, {23,"evening"},
    };
    int tf=0;
    for (unsigned i=0;i<sizeof tt/sizeof*tt;i++){
        g.hour=(uint8_t)tt[i].hour; vm.outlen=0; vm.out[0]=0;
        print_text(&vm,(const uint8_t*)"$T",2); vm.out[vm.outlen]=0;
        int ok = (strcmp(vm.out, tt[i].exp)==0);
        printf("  hour %2d -> \"%s\" (expect \"%s\") %s\n", tt[i].hour, vm.out, tt[i].exp, ok?"OK":"FAIL");
        if(!ok){ tf++; fail++; }
    }
    printf(tf ? "$T token: %d FAILURE(S)\n" : "$T token: ALL PASS\n", tf);

    /* the pooled pack (usecode_vm.md game state, §6c): gold = obj 88 in `gold`, full obj numbers,
     * actor 1 / party members are the player, GIVE/NEW quality, WEIGHT(obj,qty), CANCARRY */
    printf("\npack, gold, weight:\n");
    static uint8_t wt[1024]; wt[243]=1; wt[56]=1; wt[65]=1;
    cvm_game_init(&g, 1); g.gold=500; g.in_party[1]=1; g.in_party[2]=1; g.obj_weight=wt; g.party_room=7; g.str[9]=15;
    vm.npc=13; int pf=0;
    #define PK(nm,cond) do{ int ok_=(cond); printf("  %-38s %s\n",nm,ok_?"OK":"FAIL"); if(!ok_){ pf++; fail++; } }while(0)
    add(&vm,88,0,-3); PK("DELETE 3 gold -> 497", g.gold==497 && g.player_obj[88]==0);
    PK("OBJCOUNT gold reads the gold total", cnt(&vm,88)==497);
    PK("actor 1 is the player", is_player(&vm,1));
    PK("a party member is the player", is_player(&vm,2) && !is_player(&vm,13));
    add(&vm,396,0,1); PK("obj 396 counted as 396, not 140", g.player_obj[396]==1 && g.player_obj[140]==0);
    add(&vm,64,1,1); PK("NEW key quality 1 kept", g.player_objq[64]==1);
    { uint32_t st[8]; int sp=0; st[sp++]=243; st[sp++]=1; st[sp++]=0x9b; sp=evop(&vm,st,sp); PK("WEIGHT(rune,1) = 1", sp==1 && st[0]==1); }
    { uint32_t st[8]; int sp=0; st[sp++]=65; st[sp++]=30; st[sp++]=0x9b; sp=evop(&vm,st,sp); PK("WEIGHT(black pearl,30) = 3", sp==1 && st[0]==3); }
    { uint32_t st[8]; int sp=0; st[sp++]=1; st[sp++]=0x9a; sp=evop(&vm,st,sp); PK("CANCARRY(Avatar) = party room", sp==1 && st[0]==7); }
    { uint32_t st[8]; int sp=0; st[sp++]=9; st[sp++]=0x9a; sp=evop(&vm,st,sp); PK("CANCARRY(outsider str 15) = 300", sp==1 && st[0]==300); }
    #undef PK
    printf(pf ? "pack/gold/weight: %d FAILURE(S)\n" : "pack/gold/weight: ALL PASS\n", pf);

    return fail ? 1 : 0;
}
