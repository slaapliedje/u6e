// SPDX-License-Identifier: GPL-2.0-or-later
/* rdemo.c — M3 "first frame" demo: render a U6 map region on the TT.
 *
 * Flow: detect -> plat_mem_init -> rdr_init (TT-low 256c) -> load the asset
 * pack -> composite the tile region into the chunky backbuffer -> set palette
 * -> present (c2p to screen). Also dumps the backbuffer to OUT.RAW so the host
 * can verify the composite against the golden image. Reads pack U6.PAK from the
 * current GEMDOS drive.
 */
#include "render.h"
#include "platmem.h"
#include "platfile.h"
#include <osbind.h>

static uint16_t be16(const uint8_t *p) { return (uint16_t)((p[0] << 8) | p[1]); }

int main(void)
{
    platform_caps_t caps;
    platform_detect(&caps);
    Cconws("u6e M3 render demo: loading...\r\n");

    if (!plat_mem_init(&caps)) { Cconws("mem init failed\r\n"); Crawcin(); return 1; }

    int32_t paklen = 0;
    uint8_t *pak = (uint8_t *)plat_load_file("U6.PAK", /*fast=*/true, &paklen);
    if (!pak || paklen < 780) { Cconws("cannot load U6.PAK\r\n"); Crawcin(); return 1; }
    if (pak[0] != 'U' || pak[1] != '6' || pak[2] != 'P' || pak[3] != '1') {
        Cconws("bad pack magic\r\n"); Crawcin(); return 1;
    }

    int wt = be16(pak + 4), ht = be16(pak + 6), nt = be16(pak + 8);
    const uint8_t *palette = pak + 12;
    const uint8_t *tileset = palette + 768;
    const uint8_t *grid    = tileset + (int32_t)nt * 256;

    if (!rdr_init(&caps)) { Cconws("no renderer for this machine\r\n"); Crawcin(); return 1; }

    /* composite: blit each 16x16 tile into the chunky backbuffer */
    uint8_t *bb = rdr_backbuffer();
    int W = rdr_width();
    for (int ty = 0; ty < ht; ty++)
        for (int tx = 0; tx < wt; tx++) {
            const uint8_t *tile = tileset + (int32_t)grid[ty * wt + tx] * 256;
            for (int row = 0; row < 16; row++) {
                uint8_t *dst = bb + (int32_t)(ty * 16 + row) * W + tx * 16;
                const uint8_t *src = tile + row * 16;
                for (int col = 0; col < 16; col++) dst[col] = src[col];
            }
        }

    rdr_set_palette(palette, 256);
    rdr_present();

    /* dump actual video geometry for diagnosis */
    uint32_t vid[6];
    rdr_debug_info(vid);
    plat_file vf = plat_fcreate("VID.BIN");
    if (vf != PLAT_BADFILE) { plat_fwrite(vf, vid, (int32_t)sizeof vid); plat_fclose(vf); }

    /* dump the chunky backbuffer for host golden-image comparison */
    plat_file out = plat_fcreate("OUT.RAW");
    if (out != PLAT_BADFILE) {
        plat_fwrite(out, bb, (int32_t)W * rdr_height());
        plat_fclose(out);
    }

    /* Hold the frame on screen. (A real input loop + rdr_shutdown lands in M5;
     * headless capture needs the frame to persist, and Crawcin doesn't block
     * without a console, so loop on Vsync — reset the emulator to exit.) */
    for (;;) Vsync();
}
