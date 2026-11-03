// SPDX-License-Identifier: GPL-2.0-or-later
/* transfer.c — an Ultima IV or V saved game -> the Avatar's U6 stats (docs/formats/transfer_saves.md) */
#include "transfer.h"

static int rd16le(const uint8_t *p) { return p[0] | (p[1] << 8); }
static int rd16be(const uint8_t *p) { return (p[0] << 8) | p[1]; }

/* ROST.BIN keeps its numbers in BCD: 0x27 means 27. -1 if b isn't BCD. */
static int bcd8(uint8_t b) { return (b >> 4) > 9 || (b & 15) > 9 ? -1 : (b >> 4) * 10 + (b & 15); }
static int bcd16(const uint8_t *p)
{
    int hi = bcd8(p[0]), lo = bcd8(p[1]);
    return hi < 0 || lo < 0 ? -1 : hi * 100 + lo;
}

/* first 8 bytes of a NUL-terminated name: printable, at least one letter's worth */
static int get_name(const uint8_t *p, char *out)
{
    int i = 0;
    while (i < 8 && p[i]) {
        if (p[i] < 32 || p[i] > 126) return 0;
        out[i] = (char)p[i]; i++;
    }
    out[i] = 0;
    return i > 0;
}

/* an Ultima IV attribute (to 99) on U6's scale */
static int u4_attr(int v)
{
    if (v < 10) return v;
    if (v < 30) return 10 + (v - 9) / 2;
    return 20 + (v - 30) / 4;
}

static void convert(xfer_char_t *c)
{
    int scale = c->game == 4;
    c->new_str = scale ? u4_attr(c->str) : c->str;
    c->new_dex = scale ? u4_attr(c->dex) : c->dex;
    c->new_int = scale ? u4_attr(c->intel) : c->intel;
    c->new_magic = c->new_int;
    c->new_exp = c->exp / 10;
    c->new_level = 1;
    for (int k = c->exp / 1000; k > 0; k >>= 1) c->new_level++;
    c->new_hp = c->new_level * 30;
    if (c->new_hp > 255) c->new_hp = 255;
}

/* the checks the original makes; maxattr is 99 in Ultima IV, 30 in Ultima V */
static int in_range(const xfer_char_t *c, int maxattr, int hp, int maxhp)
{
    return c->str <= maxattr && c->dex <= maxattr && c->intel <= maxattr
        && hp <= 9999 && maxhp <= 9999 && c->exp <= 9999;
}

int xfer_parse_party_sav(const uint8_t *d, int32_t n, xfer_char_t *c)
{
    const uint8_t *r = d + 8;                           /* the first of 8 party records */
    const uint8_t *karma = d + 0x14E;
    if (n < 0x15E || !get_name(r + 0x14, c->name)) return 0;
    c->game = 4;
    c->male = r[0x24] == 0x0B;
    c->cls = r[0x25];
    if (c->cls > 7) return 0;
    c->str = rd16le(r + 6); c->dex = rd16le(r + 8); c->intel = rd16le(r + 10);
    c->magic = rd16le(r + 12);
    c->exp = rd16le(r + 4);
    int hp = rd16le(r), maxhp = rd16le(r + 2);
    c->level = maxhp / 100;
    if (!in_range(c, 99, hp, maxhp)) return 0;
    { int all0 = 1; for (int i = 0; i < 16; i++) if (karma[i]) all0 = 0;
      if (all0) c->cls = XFER_CLASS_AVATAR; }
    convert(c);
    return 1;
}

int xfer_parse_rost_bin(const uint8_t *d, int32_t n, xfer_char_t *c)
{
    if (n < 0x108 || !get_name(d, c->name)) return 0;
    c->game = 4;
    c->male = d[0x10] == 0x5C;
    c->cls = d[0x11];
    if (c->cls > 7) return 0;
    c->str = bcd8(d[0x13]); c->dex = bcd8(d[0x14]); c->intel = bcd8(d[0x15]);
    c->magic = bcd8(d[0x16]);
    c->exp = bcd16(d + 0x1C);
    int hp = bcd16(d + 0x18), maxhp = bcd16(d + 0x1A);
    if (c->str < 0 || c->dex < 0 || c->intel < 0 || c->magic < 0 || c->exp < 0 || hp < 0 || maxhp < 0) return 0;
    c->level = maxhp / 100;
    if (!in_range(c, 99, hp, maxhp)) return 0;
    { int all0 = 1; for (int i = 0; i < 8; i++) if (d[0x100 + i]) all0 = 0;
      if (all0) c->cls = XFER_CLASS_AVATAR; }
    convert(c);
    return 1;
}

/* Ultima V keeps the same record on both machines, with its words swapped */
static int parse_u5(const uint8_t *d, int big, xfer_char_t *c, int *fits)
{
    const uint8_t *r = d + 2;
    int (*rd)(const uint8_t *) = big ? rd16be : rd16le;
    if (!get_name(r, c->name)) return 0;
    c->game = 5;
    c->male = r[9] == 0x0B;
    c->cls = XFER_CLASS_AVATAR;
    c->str = r[12]; c->dex = r[13]; c->intel = r[14];
    c->magic = r[15];
    int hp = rd(r + 16), maxhp = rd(r + 18);
    c->exp = rd(r + 20);
    c->level = r[22];
    if (!in_range(c, 30, hp, maxhp)) return 0;
    *fits = maxhp == c->level * 30;
    convert(c);
    return 1;
}

int xfer_parse_saved_gam(const uint8_t *d, int32_t n, xfer_char_t *c)
{
    xfer_char_t be;
    int lefits = 0, befits = 0;
    if (n < 0x22) return 0;
    int le_ok = parse_u5(d, 0, c, &lefits);
    int be_ok = parse_u5(d, 1, &be, &befits);
    if (be_ok && (!le_ok || (befits && !lefits))) { *c = be; return 1; }
    return le_ok;
}
