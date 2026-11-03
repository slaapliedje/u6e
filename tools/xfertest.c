// SPDX-License-Identifier: GPL-2.0-or-later
/* xfertest.c — Transfer a Character's parsers and conversion (docs/formats/transfer_saves.md), on
 * saves built here. RESULT line as the other logic tests. */
#include <stdio.h>
#include <string.h>
#include "transfer.h"

static int g_fail;
#define check(what, cond) do { if (cond) printf("ok   %s\n", what); else { printf("FAIL %s\n", what); g_fail++; } } while (0)

static void le16(uint8_t *p, int v) { p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8); }
static void be16(uint8_t *p, int v) { p[0] = (uint8_t)(v >> 8); p[1] = (uint8_t)v; }

/* the stock Ultima V party leader, on either machine */
static void u5_save(uint8_t *d, int big)
{
    void (*wr)(uint8_t *, int) = big ? be16 : le16;
    uint8_t *r = d + 2;
    memset(d, 0, 64);
    memcpy(r, "ERIK", 4);
    r[9] = 0x0B; r[12] = 20; r[13] = 17; r[14] = 16; r[15] = 16;
    wr(r + 16, 60); wr(r + 18, 60); wr(r + 20, 150); r[22] = 2;
}

static void u4_dos(uint8_t *d)
{
    uint8_t *r = d + 8;
    memset(d, 0, 0x160);
    le16(r, 400); le16(r + 2, 400); le16(r + 4, 2500);
    le16(r + 6, 30); le16(r + 8, 15); le16(r + 10, 9); le16(r + 12, 40);
    memcpy(r + 0x14, "Dupre the Knight", 16);
    r[0x24] = 0x0C; r[0x25] = 5;
    for (int i = 0; i < 8; i++) le16(d + 0x14E + i * 2, 50);
}

/* numbers in BCD */
static void u4_st(uint8_t *d)
{
    memset(d, 0, 0x160);
    memcpy(d, "Geoffrey", 8);
    d[0x10] = 0x5C; d[0x11] = 2;
    d[0x13] = 0x99; d[0x14] = 0x10; d[0x15] = 0x29; d[0x16] = 0x07;
    be16(d + 0x18, 0x0800); be16(d + 0x1A, 0x0800); be16(d + 0x1C, 0x9999);
    memset(d + 0x100, 0x20, 8);
}

/* a new Mage, as Ultima IV on the ST wrote her */
static void u4_st_cara(uint8_t *d)
{
    memset(d, 0, 0x160);
    memcpy(d, "CARA", 4);
    d[0x10] = 0x7B; d[0x12] = 0xC7;
    d[0x13] = 0x19; d[0x14] = 0x18; d[0x15] = 0x27; d[0x16] = 0x20;
    be16(d + 0x18, 0x0200); be16(d + 0x1A, 0x0200); be16(d + 0x1C, 0x0125);
    memset(d + 0x100, 0x50, 8);
}

int main(void)
{
    uint8_t d[XFER_HEAD_BYTES];
    xfer_char_t c;

    u5_save(d, 1);
    check("U5 ST: found", xfer_parse_saved_gam(d, sizeof d, &c));
    check("U5 ST: name, sex, class", !strcmp(c.name, "ERIK") && c.male && c.cls == XFER_CLASS_AVATAR && c.game == 5);
    check("U5 ST: old stats", c.str == 20 && c.dex == 17 && c.intel == 16 && c.magic == 16 && c.exp == 150 && c.level == 2);
    check("U5: stats carry over", c.new_str == 20 && c.new_dex == 17 && c.new_int == 16 && c.new_magic == 16);
    check("U5: exp/10, level 1, 30 HP", c.new_exp == 15 && c.new_level == 1 && c.new_hp == 30);

    u5_save(d, 0);
    check("U5 DOS: found, same values", xfer_parse_saved_gam(d, sizeof d, &c) && c.exp == 150 && c.level == 2 && c.new_hp == 30);

    u5_save(d, 1); d[2 + 12] = 31;
    check("U5: Str over 30 is no character", !xfer_parse_saved_gam(d, sizeof d, &c));
    u5_save(d, 1); d[2] = 0;
    check("U5: empty name is no character", !xfer_parse_saved_gam(d, sizeof d, &c));
    check("U5: short file is no character", !xfer_parse_saved_gam(d, 0x10, &c));
    memset(d, 0xFF, sizeof d);
    check("U5: not a save is no character", !xfer_parse_saved_gam(d, sizeof d, &c));

    u4_dos(d);
    check("U4 DOS: found", xfer_parse_party_sav(d, sizeof d, &c));
    check("U4 DOS: 8-byte name, female, Paladin", !strcmp(c.name, "Dupre th") && !c.male && c.cls == 5 && c.game == 4);
    check("U4 DOS: old stats", c.str == 30 && c.dex == 15 && c.intel == 9 && c.magic == 40 && c.exp == 2500 && c.level == 4);
    check("U4: attributes scaled", c.new_str == 20 && c.new_dex == 13 && c.new_int == 9 && c.new_magic == 9);
    check("U4: exp 2500 -> 250, level 2+1", c.new_exp == 250 && c.new_level == 3 && c.new_hp == 90);
    for (int i = 0; i < 8; i++) le16(d + 0x14E + i * 2, 0);
    check("U4 DOS: all karma 0 is an Avatar", xfer_parse_party_sav(d, sizeof d, &c) && c.cls == XFER_CLASS_AVATAR);
    u4_dos(d); le16(d + 8 + 6, 100);
    check("U4 DOS: Str over 99 is no character", !xfer_parse_party_sav(d, sizeof d, &c));
    u4_dos(d); d[8 + 0x25] = 8;
    check("U4 DOS: class over 7 is no character", !xfer_parse_party_sav(d, sizeof d, &c));

    u4_st(d);
    check("U4 ST: found", xfer_parse_rost_bin(d, sizeof d, &c));
    check("U4 ST: name, male, Fighter", !strcmp(c.name, "Geoffrey") && c.male && c.cls == 2);
    check("U4 ST: top of the scale", c.new_str == 37 && c.new_dex == 10 && c.new_int == 20);
    check("U4 ST: exp 9999 -> 999, level 5, 150 HP", c.new_exp == 999 && c.new_level == 5 && c.new_hp == 150);
    memset(d + 0x100, 0, 8);
    check("U4 ST: all karma 0 is an Avatar", xfer_parse_rost_bin(d, sizeof d, &c) && c.cls == XFER_CLASS_AVATAR);
    u4_st(d); d[0x13] = 0x1A;
    check("U4 ST: not BCD is no character", !xfer_parse_rost_bin(d, sizeof d, &c));

    u4_st_cara(d);
    check("U4 ST, real: found", xfer_parse_rost_bin(d, sizeof d, &c));
    check("U4 ST, real: name, female, Mage", !strcmp(c.name, "CARA") && !c.male && c.cls == 0);
    check("U4 ST, real: true values", c.str == 19 && c.dex == 18 && c.intel == 27 && c.magic == 20 && c.exp == 125 && c.level == 2);
    check("U4 ST, real: converted", c.new_str == 15 && c.new_dex == 14 && c.new_int == 19 && c.new_exp == 12 && c.new_level == 1 && c.new_hp == 30);

    printf(g_fail ? "\nRESULT: %d FAILURE(S)\n" : "\nRESULT: ALL PASS\n", g_fail);
    return g_fail ? 1 : 0;
}
