// SPDX-License-Identifier: GPL-2.0-or-later
/* u6lzc.h — Savage Empire / Martian Dreams ".LZC" flagged-library reader (M9).
 *
 * Implemented FROM /docs/formats/lzc.md (clean-room). An .LZC file is one
 * flagged library: a 4-byte LE total-size header, then an index of 4-byte
 * entries (3-byte LE offset + 1-byte flag), then the item bodies. Each item is
 * either LZW-compressed (an embedded lzw.md container) or stored raw; the flag
 * selects which. This is the SE/MD replacement for U6's library + lzw layers.
 *
 * Portable C: in-memory buffers only, explicit LE reads (big-endian 68000 safe),
 * no TOS calls. Builds for the m68k target and the host asset tools alike.
 */
#ifndef U6E_U6LZC_H
#define U6E_U6LZC_H

#include <stdint.h>
#include <stdbool.h>

/* Per-game flag value that marks an LZW-compressed item. */
#define U6LZC_COMP_SE   0x01u   /* Savage Empire  */
#define U6LZC_COMP_MD   0x20u   /* Martian Dreams */
#define U6LZC_COMP_SNIFF 0x00u  /* 0 => detect compression from the item body   */
#define U6LZC_FLAG_ABSENT 0xFFu /* empty index slot                             */

typedef struct {
    const uint8_t *data;
    int32_t        len;
    int32_t        count;        /* number of index entries (incl. absent)      */
    uint8_t        comp_marker;  /* flag value meaning "LZW" (or 0 = sniff)      */
} u6lzc_t;

/* Parse the header + index. comp_marker is the game's LZW flag (U6LZC_COMP_SE/
 * _MD) or U6LZC_COMP_SNIFF to auto-detect per item. Returns false if the header
 * size disagrees with len or the index is inconsistent. */
bool u6lzc_open(u6lzc_t *lib, const uint8_t *data, int32_t len, uint8_t comp_marker);

int32_t u6lzc_count(const u6lzc_t *lib);

/* Raw on-disk slice of entry i: pointer into the buffer, *out_size = its byte
 * length, *out_flag = the entry's flag byte. Returns NULL for an absent (0xFF)
 * entry or out-of-range i. For a compressed entry the slice IS the embedded
 * lzw.md container (its own 4-byte size header + stream). */
const uint8_t *u6lzc_entry(const u6lzc_t *lib, int32_t i,
                           int32_t *out_size, uint8_t *out_flag);

/* Whether entry i is LZW-compressed (flag == comp_marker, or — when comp_marker
 * is U6LZC_COMP_SNIFF — the body carries the lzw.md signature). False for absent
 * or out-of-range i. */
bool u6lzc_is_compressed(const u6lzc_t *lib, int32_t i);

/* Materialise entry i into dst[0..dstcap): decompress if compressed, else copy.
 * Returns bytes produced, or -1 on error (absent/out-of-range/dst too small).
 * Depends on u6lzw for the compressed path. */
int32_t u6lzc_read(const u6lzc_t *lib, int32_t i, uint8_t *dst, int32_t dstcap);

#endif /* U6E_U6LZC_H */
