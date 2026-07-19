// SPDX-License-Identifier: GPL-2.0-or-later
/* u6lzc.c — SE/MD ".LZC" flagged-library reader. See /docs/formats/lzc.md. */
#include "u6lzc.h"
#include "u6lzw.h"

static uint32_t rd_u32le(const uint8_t *p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
           ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

/* Entry i's 3-byte LE offset (from byte 0 of the file). */
static int32_t ent_off(const u6lzc_t *lib, int32_t i)
{
    const uint8_t *p = lib->data + 4 + (int32_t)i * 4;
    return (int32_t)((uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16));
}

static uint8_t ent_flag(const u6lzc_t *lib, int32_t i)
{
    return lib->data[4 + (int32_t)i * 4 + 3];
}

bool u6lzc_open(u6lzc_t *lib, const uint8_t *data, int32_t len, uint8_t comp_marker)
{
    if (!lib || !data || len < 8) return false;
    if ((int32_t)rd_u32le(data) != len) return false;      /* header == file size */

    lib->data = data;
    lib->len = len;
    lib->comp_marker = comp_marker;

    int32_t first = (int32_t)((uint32_t)data[4] | ((uint32_t)data[5] << 8) |
                              ((uint32_t)data[6] << 16));
    /* first offset == end of the index; index starts at byte 4 */
    if (first < 8 || first > len || ((first - 4) % 4) != 0) return false;
    lib->count = (first - 4) / 4;
    return true;
}

int32_t u6lzc_count(const u6lzc_t *lib) { return lib ? lib->count : 0; }

const uint8_t *u6lzc_entry(const u6lzc_t *lib, int32_t i,
                           int32_t *out_size, uint8_t *out_flag)
{
    if (out_size) *out_size = 0;
    if (out_flag) *out_flag = U6LZC_FLAG_ABSENT;
    if (!lib || i < 0 || i >= lib->count) return (const uint8_t *)0;

    uint8_t flag = ent_flag(lib, i);
    if (flag == U6LZC_FLAG_ABSENT) return (const uint8_t *)0;   /* empty slot */

    int32_t start = ent_off(lib, i);
    /* End = next present (non-absent) entry's offset, or EOF. */
    int32_t end = lib->len;
    for (int32_t j = i + 1; j < lib->count; j++) {
        if (ent_flag(lib, j) != U6LZC_FLAG_ABSENT) { end = ent_off(lib, j); break; }
    }
    if (start < 4 || start > lib->len || end < start || end > lib->len)
        return (const uint8_t *)0;

    if (out_size) *out_size = end - start;
    if (out_flag) *out_flag = flag;
    return lib->data + start;
}

bool u6lzc_is_compressed(const u6lzc_t *lib, int32_t i)
{
    int32_t size; uint8_t flag;
    const uint8_t *p = u6lzc_entry(lib, i, &size, &flag);
    if (!p) return false;

    if (lib->comp_marker != U6LZC_COMP_SNIFF)
        return flag == lib->comp_marker;

    /* Sniff: an lzw.md container declares an unpacked size > the stored span,
     * then a 9-bit LSB-first clear code (byte 0x00, next byte bit 0 set). */
    if (size < 6) return false;
    int32_t decl = (int32_t)rd_u32le(p);
    if (decl <= size - 4) return false;
    return p[4] == 0x00 && (p[5] & 1u) != 0;
}

int32_t u6lzc_read(const u6lzc_t *lib, int32_t i, uint8_t *dst, int32_t dstcap)
{
    int32_t size; uint8_t flag;
    const uint8_t *p = u6lzc_entry(lib, i, &size, &flag);
    if (!p || !dst) return -1;

    if (u6lzc_is_compressed(lib, i))
        return u6lzw_decompress(p, size, dst, dstcap);   /* embedded lzw.md item */

    if (size > dstcap) return -1;                        /* stored: raw copy */
    for (int32_t k = 0; k < size; k++) dst[k] = p[k];
    return size;
}
