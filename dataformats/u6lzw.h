/* u6lzw.h — Ultima VI LZW container decoder (portable, M2).
 *
 * Implemented FROM /docs/formats/lzw.md (clean-room). Portable C: works on
 * in-memory byte buffers only — NO TOS calls, NO endianness assumptions about
 * the host. Compiles for both the m68k target and the host asset tools.
 *
 * Source files are little-endian; this code reads the LE size header and the
 * LSB-first bit stream explicitly, so it is correct on a big-endian 68000.
 */
#ifndef U6E_U6LZW_H
#define U6E_U6LZW_H

#include <stdint.h>

/* Declared decompressed size from the 4-byte LE header. 0 means the body is
 * stored (uncompressed). Returns -1 if src is too short. */
int32_t u6lzw_unpacked_size(const uint8_t *src, int32_t srclen);

/* Decompress (or copy, if stored) the whole container in src[0..srclen) into
 * dst[0..dstcap). Returns the number of bytes produced, or -1 on error
 * (bad input / dst too small). */
int32_t u6lzw_decompress(const uint8_t *src, int32_t srclen,
                         uint8_t *dst, int32_t dstcap);

#endif /* U6E_U6LZW_H */
