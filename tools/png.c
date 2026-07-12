/* png.c — minimal RGB PNG writer. Host-only. */
#include "png.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static uint32_t crc_table[256];
static int crc_ready = 0;
static void crc_init(void)
{
    for (uint32_t n = 0; n < 256; n++) {
        uint32_t c = n;
        for (int k = 0; k < 8; k++) c = (c & 1) ? 0xEDB88320u ^ (c >> 1) : (c >> 1);
        crc_table[n] = c;
    }
    crc_ready = 1;
}
static uint32_t crc32_buf(uint32_t crc, const uint8_t *p, size_t n)
{
    if (!crc_ready) crc_init();
    crc ^= 0xFFFFFFFFu;
    for (size_t i = 0; i < n; i++) crc = crc_table[(crc ^ p[i]) & 0xFF] ^ (crc >> 8);
    return crc ^ 0xFFFFFFFFu;
}

static void put_be32(uint8_t *p, uint32_t v)
{ p[0] = v >> 24; p[1] = v >> 16; p[2] = v >> 8; p[3] = v; }

/* write one PNG chunk: len, type, data, crc(type+data) */
static void chunk(FILE *f, const char *type, const uint8_t *data, uint32_t len)
{
    uint8_t hdr[8];
    put_be32(hdr, len);
    memcpy(hdr + 4, type, 4);
    fwrite(hdr, 1, 8, f);
    if (len) fwrite(data, 1, len, f);

    /* CRC is computed over type + data together */
    uint8_t *tmp = malloc(4 + len);
    memcpy(tmp, type, 4);
    if (len) memcpy(tmp + 4, data, len);
    uint32_t crc = crc32_buf(0, tmp, 4 + len);
    free(tmp);
    uint8_t c[4]; put_be32(c, crc); fwrite(c, 1, 4, f);
}

/* zlib stream wrapping `raw` using stored DEFLATE blocks */
static uint8_t *zlib_store(const uint8_t *raw, uint32_t rawlen, uint32_t *outlen)
{
    uint32_t nblocks = (rawlen + 65534) / 65535; if (nblocks == 0) nblocks = 1;
    uint32_t cap = 2 + nblocks * 5 + rawlen + 4;
    uint8_t *out = malloc(cap);
    uint32_t o = 0;
    out[o++] = 0x78; out[o++] = 0x01;            /* zlib header (no compression) */
    uint32_t pos = 0;
    do {
        uint32_t n = rawlen - pos; if (n > 65535) n = 65535;
        out[o++] = (pos + n >= rawlen) ? 1 : 0;  /* BFINAL on last block */
        out[o++] = n & 0xFF; out[o++] = (n >> 8) & 0xFF;
        uint16_t nl = ~(uint16_t)n;
        out[o++] = nl & 0xFF; out[o++] = (nl >> 8) & 0xFF;
        memcpy(out + o, raw + pos, n); o += n; pos += n;
    } while (pos < rawlen);
    /* adler32 */
    uint32_t a = 1, b = 0;
    for (uint32_t i = 0; i < rawlen; i++) { a = (a + raw[i]) % 65521; b = (b + a) % 65521; }
    put_be32(out + o, (b << 16) | a); o += 4;
    *outlen = o;
    return out;
}

int png_write_rgb(const char *path, int width, int height, const uint8_t *rgb)
{
    FILE *f = fopen(path, "wb");
    if (!f) return -1;

    static const uint8_t sig[8] = { 137, 80, 78, 71, 13, 10, 26, 10 };
    fwrite(sig, 1, 8, f);

    uint8_t ihdr[13];
    put_be32(ihdr, (uint32_t)width);
    put_be32(ihdr + 4, (uint32_t)height);
    ihdr[8] = 8;       /* bit depth */
    ihdr[9] = 2;       /* color type 2 = truecolor RGB */
    ihdr[10] = 0; ihdr[11] = 0; ihdr[12] = 0;
    chunk(f, "IHDR", ihdr, 13);

    /* raw image: each row prefixed with filter byte 0 */
    uint32_t stride = (uint32_t)width * 3;
    uint32_t rawlen = (stride + 1) * (uint32_t)height;
    uint8_t *raw = malloc(rawlen);
    for (int y = 0; y < height; y++) {
        raw[y * (stride + 1)] = 0;
        memcpy(raw + y * (stride + 1) + 1, rgb + (size_t)y * stride, stride);
    }
    uint32_t zlen; uint8_t *z = zlib_store(raw, rawlen, &zlen);
    chunk(f, "IDAT", z, zlen);
    chunk(f, "IEND", NULL, 0);

    free(raw); free(z); fclose(f);
    return 0;
}
