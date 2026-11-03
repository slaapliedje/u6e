// SPDX-License-Identifier: GPL-2.0-or-later
/* platfile.h — GEMDOS file I/O wrappers (M1).
 *
 * Thin, endianness-agnostic byte I/O. Multi-byte field decoding (LE->host) is
 * /dataformats' job, not ours; this layer just moves bytes. The headline helper
 * is plat_load_file: pull a whole data file into a pool in one shot (the
 * "read big, parse in memory" pattern), with DMA-safe handling when the
 * destination is Alt-RAM.
 */
#ifndef U6E_PLATFILE_H
#define U6E_PLATFILE_H

#include <stdint.h>
#include <stdbool.h>

typedef int16_t plat_file;        /* GEMDOS handle; < 0 means error */
#define PLAT_BADFILE  ((plat_file)-1)

enum { PLAT_RDONLY = 0, PLAT_WRONLY = 1, PLAT_RDWR = 2 };
enum { PLAT_SEEK_SET = 0, PLAT_SEEK_CUR = 1, PLAT_SEEK_END = 2 };

plat_file plat_fopen(const char *path, int mode);
plat_file plat_fcreate(const char *path);
int       plat_fclose(plat_file f);

/* Byte transfer. plat_fread bounces through an ST-RAM buffer automatically when
 * dst is in Alt-RAM (GEMDOS disk DMA targets chip RAM). Return bytes moved, or
 * a negative GEMDOS error. */
int32_t   plat_fread(plat_file f, void *dst, int32_t len);
int32_t   plat_fwrite(plat_file f, const void *src, int32_t len);
int32_t   plat_fseek(plat_file f, int32_t off, int whence);
int32_t   plat_fsize(plat_file f);   /* size in bytes; restores position */
uint32_t  plat_drvmap(void);         /* the drives present: bit 0 = A, bit 1 = B, ... (BIOS Drvmap 0x0A) */

/* Open path, allocate (fast ? plat_alloc_fast : plat_alloc_chip), read it all,
 * close. Returns buffer (NULL on any error); *out_len gets the byte count. */
void     *plat_load_file(const char *path, bool fast, int32_t *out_len);

/* Size of a file by name (Fopen/Fseek/Fclose), or -1 if it can't be opened. Lets a loader
 * plan memory (resident vs streamed) BEFORE reading anything. */
int32_t   plat_fsize_path(const char *path);

/* Read a whole file into a caller-owned buffer of `cap` bytes (no pool allocation): returns the
 * byte count, or -1 if absent/unreadable/bigger than cap. For small, repeatedly (re)loaded files
 * (a song, a config) that must not bump-allocate every time. */
int32_t   plat_load_into(const char *path, void *dst, int32_t cap);

/* Read `len` bytes at absolute offset `off` of an open file (Fseek + plat_fread). Returns bytes
 * read (== len on success). The streaming primitive for sectioned packs / indexed libraries. */
int32_t   plat_read_at(plat_file f, int32_t off, void *dst, int32_t len);

#endif /* U6E_PLATFILE_H */
