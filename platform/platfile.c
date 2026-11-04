// SPDX-License-Identifier: GPL-2.0-or-later
/* platfile.c — GEMDOS file I/O.
 *
 * TOS calls used (named for verification):
 *   Fopen 0x3D, Fcreate 0x3C, Fclose 0x3E, Fread 0x3F, Fwrite 0x40, Fseek 0x42; BIOS Drvmap 0x0A.
 *
 * Alt-RAM read policy (decided here): GEMDOS disk transfers DMA into chip
 * (ST) RAM, so reading directly into an Alt/TT-RAM destination is unsafe on
 * older TOS. We therefore bounce Alt-RAM reads through a fixed ST-RAM buffer.
 * The buffer is `static` (program BSS, which loads in ST-RAM by default), so it
 * needs no pool and no init ordering.
 */
#include "platfile.h"
#include "platmem.h"   /* plat_addr_is_fastram, plat_alloc_* */

#include <osbind.h>
#include <string.h>

#define BOUNCE_SIZE  (16u * 1024u)
static uint8_t g_bounce[BOUNCE_SIZE];   /* ST-RAM (BSS) DMA bounce buffer */

plat_file plat_fopen(const char *path, int mode)
{
    long h = Fopen(path, (short)mode);
    return (h < 0) ? PLAT_BADFILE : (plat_file)h;
}

plat_file plat_fcreate(const char *path)
{
    long h = Fcreate(path, 0);   /* attr 0 = normal file */
    return (h < 0) ? PLAT_BADFILE : (plat_file)h;
}

int plat_fclose(plat_file f) { return (int)Fclose((short)f); }

int32_t plat_fread(plat_file f, void *dst, int32_t len)
{
    /* Direct read when the destination is chip RAM. */
    if (!plat_addr_is_fastram(dst))
        return (int32_t)Fread((short)f, (long)len, dst);

    /* Alt-RAM destination: read in chunks via the ST-RAM bounce buffer. */
    uint8_t *out = (uint8_t *)dst;
    int32_t total = 0;
    while (len > 0) {
        int32_t chunk = (len < (int32_t)BOUNCE_SIZE) ? len : (int32_t)BOUNCE_SIZE;
        long got = Fread((short)f, (long)chunk, g_bounce);
        if (got < 0) return (int32_t)got;      /* GEMDOS error */
        if (got == 0) break;                    /* EOF */
        memcpy(out, g_bounce, (size_t)got);     /* (was a byte loop: a 1.8 MB read into TT-RAM took seconds) */
        out   += got;
        total += (int32_t)got;
        len   -= (int32_t)got;
        if (got < chunk) break;                 /* short read => EOF */
    }
    return total;
}

int32_t plat_fwrite(plat_file f, const void *src, int32_t len)
{
    if (!plat_addr_is_fastram(src))
        return (int32_t)Fwrite((short)f, (long)len, src);

    const uint8_t *in = (const uint8_t *)src;
    int32_t total = 0;
    while (len > 0) {
        int32_t chunk = (len < (int32_t)BOUNCE_SIZE) ? len : (int32_t)BOUNCE_SIZE;
        for (int32_t i = 0; i < chunk; i++) g_bounce[i] = in[i];
        long put = Fwrite((short)f, (long)chunk, g_bounce);
        if (put < 0) return (int32_t)put;
        in    += put;
        total += (int32_t)put;
        len   -= (int32_t)put;
        if (put < chunk) break;
    }
    return total;
}

int32_t plat_fseek(plat_file f, int32_t off, int whence)
{
    return (int32_t)Fseek((long)off, (short)f, (short)whence);
}

int32_t plat_fsize(plat_file f)
{
    int32_t cur = (int32_t)Fseek(0L,  (short)f, PLAT_SEEK_CUR);
    int32_t end = (int32_t)Fseek(0L,  (short)f, PLAT_SEEK_END);
    (void)Fseek((long)cur, (short)f, PLAT_SEEK_SET);   /* restore */
    return end;
}

void *plat_load_file(const char *path, bool fast, int32_t *out_len)
{
    if (out_len) *out_len = 0;

    plat_file f = plat_fopen(path, PLAT_RDONLY);
    if (f == PLAT_BADFILE) return (void *)0;

    int32_t size = plat_fsize(f);
    if (size <= 0) { plat_fclose(f); return (void *)0; }

    void *buf = fast ? plat_alloc_fast((uint32_t)size)
                     : plat_alloc_chip((uint32_t)size);
    if (!buf) { plat_fclose(f); return (void *)0; }

    int32_t got = plat_fread(f, buf, size);
    plat_fclose(f);
    if (got != size) return (void *)0;          /* short/failed read */

    if (out_len) *out_len = size;
    return buf;
}

int32_t plat_fdelete(const char *path){ return (int32_t)Fdelete(path); }
int32_t plat_frename(const char *from, const char *to){ return (int32_t)Frename(0, from, to); }

uint32_t plat_drvmap(void)
{
    return (uint32_t)Drvmap();
}

int32_t plat_fsize_path(const char *path)
{
    plat_file f = plat_fopen(path, PLAT_RDONLY);
    if (f == PLAT_BADFILE) return -1;
    int32_t n = plat_fsize(f);
    plat_fclose(f);
    return n;
}

int32_t plat_load_into(const char *path, void *dst, int32_t cap)
{
    plat_file f = plat_fopen(path, PLAT_RDONLY);
    if (f == PLAT_BADFILE) return -1;
    int32_t size = plat_fsize(f);
    if (size <= 0 || size > cap) { plat_fclose(f); return -1; }
    int32_t got = plat_fread(f, dst, size);
    plat_fclose(f);
    return (got == size) ? got : -1;
}

int32_t plat_read_at(plat_file f, int32_t off, void *dst, int32_t len)
{
    if (plat_fseek(f, off, PLAT_SEEK_SET) < 0) return -1;
    return plat_fread(f, dst, len);
}
