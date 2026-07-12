/* u6lib.h — Ultima VI "library" container reader (portable, M2).
 *
 * Implemented FROM /docs/formats/library.md (clean-room). A library is an index
 * of little-endian offsets followed by the data blocks they point at. Offsets
 * are uniformly 2 OR 4 bytes; the first offset equals the index size, so the
 * entry count is derivable. An offset of 0 marks an absent/empty entry.
 *
 * Operates on an in-memory buffer that is ALREADY LZW-decompressed if the file
 * was compressed (caller runs u6lzw first). Portable C, big-endian-safe.
 */
#ifndef U6E_U6LIB_H
#define U6E_U6LIB_H

#include <stdint.h>
#include <stdbool.h>

typedef struct {
    const uint8_t *data;
    int32_t        len;
    int            offset_width;  /* 2 or 4 */
    int32_t        index_base;    /* 0, or 4 when a size prefix precedes index */
    int32_t        count;         /* number of index entries */
} u6lib_t;

/* Open with a KNOWN layout (the normal path — loaders know each file's format).
 * has_size_prefix: a 4-byte LE total-size word precedes the index. Returns
 * false if the layout is inconsistent with len. */
bool u6lib_open(u6lib_t *lib, const uint8_t *data, int32_t len,
                int offset_width, bool has_size_prefix);

/* Convenience for tools: guess offset width (and size prefix) from the bytes.
 * Returns false if neither 2- nor 4-byte interpretation is self-consistent. */
bool u6lib_open_auto(u6lib_t *lib, const uint8_t *data, int32_t len);

int32_t u6lib_count(const u6lib_t *lib);

/* Pointer to entry i's data; *out_size gets its byte length. Returns NULL for
 * an absent entry (offset 0) or out-of-range i. */
const uint8_t *u6lib_entry(const u6lib_t *lib, int32_t i, int32_t *out_size);

#endif /* U6E_U6LIB_H */
