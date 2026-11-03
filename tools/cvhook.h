// SPDX-License-Identifier: GPL-2.0-or-later
/* cvhook.h — progress hooks for the Atari converter (CONVERT.TOS, tools/convert.c).
 * cv_step(label) marks the start of a named stage; cv_tick() is called often inside long stages so
 * the converter can update its clock. Here they are weak no-ops, so the host tools are unchanged;
 * convert.c defines the real ones. */
#ifndef U6E_CVHOOK_H
#define U6E_CVHOOK_H
void cv_step(const char *label);
void cv_tick(void);
__attribute__((weak)) void cv_step(const char *label) { (void)label; }
__attribute__((weak)) void cv_tick(void) {}
#endif
