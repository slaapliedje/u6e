/* u6conv.c — conversation parser + minimal VM (M4 spike). See u6conv.h. */
#include "u6conv.h"

/* Conversation control opcodes (observed; see /docs/formats/converse.md). */
enum {
    OP_START = 0xFF,  /* + NPC number byte                     */
    OP_DESC  = 0xF1,  /* name -> look description              */
    OP_GREET = 0xF2,  /* expression -> greeting (quoted text)  */
    OP_ASK   = 0xF7,  /* begin keyword input loop             */
    OP_KEYW  = 0xEF,  /* keyword section: keys, then OP_RESP   */
    OP_RESP  = 0xF6,  /* keyword list -> response             */
    OP_REPMT = 0xB0,  /* response end (+4 arg bytes), re-ask  */
    OP_BYE   = 0xB6,  /* end conversation                     */
    OP_END   = 0xEE   /* end of conversation data             */
};

static bool is_text(uint8_t b) { return (b >= 0x20 && b <= 0x7E) || b == 0x0A; }
static bool is_ctrl(uint8_t b) { return b >= 0x80; }   /* structural opcode */

static char lc(char ch) { return (ch >= 'A' && ch <= 'Z') ? (char)(ch + 32) : ch; }

/* Copy a display-text run starting at *pos into dst (size cap): emit printable
 * bytes + newlines, substitute "$P" with player, strip '@' highlight markers,
 * convert newline to space. Stops at the first structural opcode (>=0x80).
 * Skips any leading non-text bytes (e.g. an F2 expression) first. */
static void read_text(const uint8_t *s, int32_t len, int32_t *pos,
                      char *dst, int cap, const char *player)
{
    int32_t p = *pos;
    while (p < len && !is_text(s[p]) && !is_ctrl(s[p])) p++;  /* skip non-text, non-opcode */
    int o = 0;
    while (p < len && !is_ctrl(s[p])) {
        uint8_t b = s[p];
        if (b == '$' && p + 1 < len) {                        /* substitution token */
            char tok = (char)s[p + 1];
            const char *sub = (tok == 'P') ? player          /* player name      */
                            : (tok == 'G') ? "milord"        /* gender/title     */
                            : "";                             /* unknown: drop    */
            for (const char *q = sub; *q && o < cap - 1; q++) dst[o++] = *q;
            p += 2; continue;
        }
        if (b == '@') { p++; continue; }                      /* highlight marker */
        if (b == 0x0A) { if (o < cap - 1) dst[o++] = ' '; p++; continue; }
        if (is_text(b) && o < cap - 1) dst[o++] = (char)b;
        p++;
    }
    while (o > 0 && dst[o - 1] == ' ') o--;                   /* trim trailing */
    dst[o] = '\0';
    *pos = p;
}

/* Copy bare ASCII (keywords) until a structural opcode. */
static void read_bare(const uint8_t *s, int32_t len, int32_t *pos, char *dst, int cap)
{
    int32_t p = *pos; int o = 0;
    while (p < len && !is_ctrl(s[p]) && s[p] != 0x00) {
        if (s[p] >= 0x20 && s[p] <= 0x7E && o < cap - 1) dst[o++] = (char)s[p];
        p++;
    }
    dst[o] = '\0';
    *pos = p;
}

bool u6conv_parse(u6conv_t *c, const uint8_t *s, int32_t len, const char *player)
{
    if (!s || len < 3 || s[0] != OP_START) return false;
    c->npc = s[1];
    c->nsect = 0;
    c->default_sect = -1;
    c->name[0] = c->desc[0] = c->greeting[0] = '\0';

    int32_t p = 2;
    read_bare(s, len, &p, c->name, sizeof c->name);           /* NPC name */

    /* description: after OP_DESC up to OP_GREET */
    if (p < len && s[p] == OP_DESC) {
        p++;
        read_text(s, len, &p, c->desc, sizeof c->desc, player);
    }
    /* greeting: after OP_GREET there is a usecode expression (high bytes) then
     * the quoted greeting text. Skip the expression up to the first printable
     * byte (the opening quote), bounded so we never cross into the input loop. */
    if (p < len && s[p] == OP_GREET) {
        p++;
        while (p < len && !(s[p] >= 0x20 && s[p] <= 0x7E) &&
               s[p] != OP_ASK && s[p] != OP_KEYW) p++;
        read_text(s, len, &p, c->greeting, sizeof c->greeting, player);
    }

    /* keyword sections */
    while (p < len && c->nsect < U6CONV_MAX_SECT) {
        if (s[p] == OP_KEYW) {
            p++;
            u6conv_sect_t *sec = &c->sect[c->nsect];
            sec->is_bye = false;
            read_bare(s, len, &p, sec->keys, sizeof sec->keys);
            if (p < len && s[p] == OP_RESP) {
                p++;
                read_text(s, len, &p, sec->resp, sizeof sec->resp, player);
            } else {
                sec->resp[0] = '\0';
            }
            /* terminator after response */
            if (p < len && s[p] == OP_BYE) sec->is_bye = true;
            /* '*' is the catch-all fallback */
            if (sec->keys[0] == '*' && sec->keys[1] == '\0') c->default_sect = c->nsect;
            /* "bye" keyword also ends the conversation */
            if ((sec->keys[0]=='b'||sec->keys[0]=='B') &&
                (sec->keys[1]=='y'||sec->keys[1]=='Y') &&
                (sec->keys[2]=='e'||sec->keys[2]=='E')) sec->is_bye = true;
            c->nsect++;
        } else {
            p++;   /* skip opcodes/args (B0 + args, B6, EE, F7, etc.) */
        }
    }
    return true;
}

/* first-4-char, case-insensitive compare of an input word vs a keyword */
static bool kw_match(const char *kw, const char *in)
{
    for (int i = 0; i < 4; i++) {
        char a = lc(kw[i]), b = lc(in[i]);
        if (a == '\0' || b == '\0') return a == b || (i > 0);  /* short words match on prefix */
        if (a != b) return false;
    }
    return true;
}

const char *u6conv_respond(const u6conv_t *c, const char *input, bool *is_bye)
{
    if (is_bye) *is_bye = false;
    for (int i = 0; i < c->nsect; i++) {
        const char *k = c->sect[i].keys;
        while (*k) {
            char one[16]; int o = 0;
            while (*k && *k != ',' && o < 15) one[o++] = *k++;
            one[o] = '\0';
            if (*k == ',') k++;
            if (one[0] && one[0] != '*' && kw_match(one, input)) {
                if (is_bye) *is_bye = c->sect[i].is_bye;
                return c->sect[i].resp;
            }
        }
    }
    if (c->default_sect >= 0) return c->sect[c->default_sect].resp;
    return (const char *)0;
}
