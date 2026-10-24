#include "text_fold.h"

#include <stdint.h>

/* U+00C0..U+00FF folded to ASCII. */
static const char LATIN1_FOLD[65] =
    "AAAAAAACEEEEIIII" "DNOOOOOxOUUUUYTs"
    "aaaaaaaceeeeiiii" "dnooooo/ouuuuyty";

static char fold(uint32_t cp)
{
    if (cp < 0x80) {
        return (char)cp;
    }
    if (cp >= 0xC0 && cp <= 0xFF) {
        return LATIN1_FOLD[cp - 0xC0];
    }
    switch (cp) {
    case 0xA0:                          /* no-break space */
        return ' ';
    case 0x2018: case 0x2019: case 0x201A: case 0x2032:
        return '\'';
    case 0x201C: case 0x201D: case 0x201E: case 0x2033:
        return '"';
    case 0x2010: case 0x2011: case 0x2012: case 0x2013: case 0x2014: case 0x2212:
        return '-';
    case 0x2026:                        /* ellipsis: one '.' keeps the length simple */
        return '.';
    default:
        return '?';
    }
}

void text_fold_ascii(char *dst, size_t size, const char *utf8)
{
    size_t o = 0;
    const unsigned char *s = (const unsigned char *)utf8;
    while (*s && o + 1 < size) {
        uint32_t cp;
        int n;
        if (*s < 0x80) {
            cp = *s;
            n = 1;
        } else if ((*s & 0xE0) == 0xC0 && (s[1] & 0xC0) == 0x80) {
            cp = ((uint32_t)(s[0] & 0x1F) << 6) | (s[1] & 0x3F);
            n = 2;
        } else if ((*s & 0xF0) == 0xE0 && (s[1] & 0xC0) == 0x80 && (s[2] & 0xC0) == 0x80) {
            cp = ((uint32_t)(s[0] & 0x0F) << 12) | ((uint32_t)(s[1] & 0x3F) << 6) | (s[2] & 0x3F);
            n = 3;
        } else if ((*s & 0xF8) == 0xF0 && (s[1] & 0xC0) == 0x80 && (s[2] & 0xC0) == 0x80 && (s[3] & 0xC0) == 0x80) {
            cp = 0x10000;       /* astral plane: nothing to fold to */
            n = 4;
        } else {
            cp = 0xFFFD;        /* invalid byte: skip just it */
            n = 1;
        }
        dst[o++] = fold(cp);
        s += n;
    }
    dst[o] = '\0';
}
