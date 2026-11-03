// SPDX-License-Identifier: GPL-2.0-or-later
/* transfer.h — an Ultima IV or V saved game -> the Avatar's U6 stats (docs/formats/transfer_saves.md) */
#ifndef U6E_TRANSFER_H
#define U6E_TRANSFER_H

#include <stdint.h>

#define XFER_CLASS_AVATAR 8

typedef struct {
    int     game;                       /* 4 or 5: the Ultima it came from */
    char    name[9];
    int     male;                       /* 1 male, 0 female */
    int     cls;                        /* 0..7, or XFER_CLASS_AVATAR */
    int     str, dex, intel, magic;     /* as in the old game */
    int     exp, level;
    int     new_str, new_dex, new_int;  /* as in U6 */
    int     new_magic, new_exp, new_level, new_hp;
} xfer_char_t;

/* how much of a file the parsers need */
#define XFER_HEAD_BYTES 0x160

/* each returns 1 and fills c if the first character in d[0..n) is a valid one, else 0 */
int xfer_parse_party_sav(const uint8_t *d, int32_t n, xfer_char_t *c);   /* Ultima IV, DOS: PARTY.SAV */
int xfer_parse_rost_bin(const uint8_t *d, int32_t n, xfer_char_t *c);    /* Ultima IV, ST: ROST.BIN */
int xfer_parse_saved_gam(const uint8_t *d, int32_t n, xfer_char_t *c);   /* Ultima V, DOS or ST: SAVED.GAM */

#endif /* U6E_TRANSFER_H */
