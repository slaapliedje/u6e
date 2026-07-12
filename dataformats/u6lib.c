// SPDX-License-Identifier: GPL-2.0-or-later
/* u6lib.c — Ultima VI library container reader. See /docs/formats/library.md. */
#include "u6lib.h"

static int32_t rd_off(const uint8_t *p, int width)
{
    if (width == 2)
        return (int32_t)((uint32_t)p[0] | ((uint32_t)p[1] << 8));
    return (int32_t)((uint32_t)p[0]        | ((uint32_t)p[1] << 8) |
                     ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24));
}

/* Read the raw index slot k (0-based) as a little-endian offset. */
static int32_t slot(const u6lib_t *lib, int32_t k)
{
    return rd_off(lib->data + lib->index_base + (int32_t)k * lib->offset_width,
                  lib->offset_width);
}

bool u6lib_open(u6lib_t *lib, const uint8_t *data, int32_t len,
                int offset_width, bool has_size_prefix)
{
    if (!data || (offset_width != 2 && offset_width != 4)) return false;

    int32_t base = has_size_prefix ? 4 : 0;
    if (len < base + offset_width) return false;

    lib->data = data;
    lib->len = len;
    lib->offset_width = offset_width;
    lib->index_base = base;

    int32_t first = rd_off(data + base, offset_width);
    /* The first offset is measured from index_base and equals the index size. */
    if (first <= 0 || (first % offset_width) != 0) return false;
    if (base + first > len) return false;

    lib->count = first / offset_width;
    return true;
}

/* Self-consistency test for one interpretation: first offset divisible by width
 * and in range, and the first few present offsets strictly increase in bounds. */
static bool plausible(const uint8_t *data, int32_t len, int width, int32_t base)
{
    if (len < base + width) return false;
    int32_t first = rd_off(data + base, width);
    if (first <= 0 || (first % width) != 0 || base + first > len) return false;

    int32_t count = first / width;
    int32_t prev = first;            /* first data block starts at `first` */
    int32_t checked = 0;
    for (int32_t i = 1; i < count && checked < 8; i++) {
        int32_t o = rd_off(data + base + i * width, width);
        if (o == 0) continue;        /* absent entry */
        if (o < prev || base + o > len) return false;
        prev = o; checked++;
    }
    return true;
}

bool u6lib_open_auto(u6lib_t *lib, const uint8_t *data, int32_t len)
{
    if (!data || len < 2) return false;

    /* Optional 4-byte total-size prefix: first word equals the file length. */
    bool prefix = (len >= 4) &&
                  (rd_off(data, 4) == len || rd_off(data, 4) == len - 4);
    int32_t base = prefix ? 4 : 0;

    /* Prefer 4-byte offsets when that reading is self-consistent (its strictly
     * increasing offsets disambiguate it from a 2-byte misread, which tends to
     * produce interleaved zero entries). */
    if (plausible(data, len, 4, base))
        return u6lib_open(lib, data, len, 4, prefix);
    if (plausible(data, len, 2, base))
        return u6lib_open(lib, data, len, 2, prefix);
    return false;
}

int32_t u6lib_count(const u6lib_t *lib) { return lib->count; }

const uint8_t *u6lib_entry(const u6lib_t *lib, int32_t i, int32_t *out_size)
{
    if (out_size) *out_size = 0;
    if (i < 0 || i >= lib->count) return (const uint8_t *)0;

    int32_t start = slot(lib, i);
    if (start == 0) return (const uint8_t *)0;     /* absent entry */
    start += lib->index_base;

    /* End = next present entry's start (or end of buffer). */
    int32_t end = lib->len;
    for (int32_t j = i + 1; j < lib->count; j++) {
        int32_t o = slot(lib, j);
        if (o != 0) { end = lib->index_base + o; break; }
    }
    if (start > lib->len || end < start || end > lib->len) return (const uint8_t *)0;

    if (out_size) *out_size = end - start;
    return lib->data + start;
}
