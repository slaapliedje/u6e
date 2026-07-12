// SPDX-License-Identifier: GPL-2.0-or-later
/* u6conv.h — Ultima VI conversation parser + minimal VM (M4 SPIKE).
 *
 * SPIKE-LEVEL: de-risks the usecode "wall". It parses the readable layer of a
 * decompressed conversation script — name, look-description, greeting, and the
 * keyword->response sections — enough to run a real conversation end to end.
 * It does NOT yet evaluate the usecode expression/flag/branch opcodes (the F2
 * expression bytes, B0 args, etc. are skipped). The full VM is M6 work; see
 * /docs/rules/usecode_vm.md. Spoken text in U6 is always double-quoted, which
 * lets this layer parse robustly without an expression evaluator.
 *
 * Input is ONE already-LZW-decompressed conversation script (the caller pulls
 * it out of CONVERSE.A/.B via the library index + u6lzw).
 */
#ifndef U6E_U6CONV_H
#define U6E_U6CONV_H

#include <stdint.h>
#include <stdbool.h>

#define U6CONV_MAX_SECT 48
#define U6CONV_NAME     32
#define U6CONV_DESC     384
#define U6CONV_TEXT     768
#define U6CONV_KEYS     96

typedef struct {
    char keys[U6CONV_KEYS];   /* raw keyword list, comma-separated */
    char resp[U6CONV_TEXT];   /* cleaned response text             */
    bool is_bye;              /* ends the conversation             */
} u6conv_sect_t;

typedef struct {
    int           npc;
    char          name[U6CONV_NAME];
    char          desc[U6CONV_DESC];       /* look description */
    char          greeting[U6CONV_TEXT];
    u6conv_sect_t sect[U6CONV_MAX_SECT];
    int           nsect;
    int           default_sect;            /* index of the '*' fallback, or -1 */
} u6conv_t;

/* Parse a decompressed script. player_name substitutes for "$P". */
bool u6conv_parse(u6conv_t *c, const uint8_t *script, int32_t len,
                  const char *player_name);

/* Find the response for an input word (4-char, case-insensitive match against
 * each section's comma-separated keywords). Falls back to the '*' section.
 * Returns NULL only if there is no match and no default. Sets *is_bye. */
const char *u6conv_respond(const u6conv_t *c, const char *input, bool *is_bye);

#endif
