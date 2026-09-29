#ifndef LUTIN_TEXT_H
#define LUTIN_TEXT_H
#include <stddef.h>
#include <stdint.h>
#include <string.h>

enum { TEXT_FIRST_GLYPH = 32, TEXT_GLYPHS = 224, TEXT_GLYPH_BYTES = 8 };

static inline unsigned text_codepoint(const char **cursor) {
    const unsigned char *p = (const unsigned char *)*cursor;
    unsigned c = *p;
    if (!c)
        return 0;
    (*cursor)++;
    if (c < 128)
        return c;
    unsigned count, value, minimum;
    if (c >= 0xc2 && c <= 0xdf) {
        count = 1;
        value = c & 31;
        minimum = 128;
    } else if (c >= 0xe0 && c <= 0xef) {
        count = 2;
        value = c & 15;
        minimum = 2048;
    } else if (c >= 0xf0 && c <= 0xf4) {
        count = 3;
        value = c & 7;
        minimum = 65536;
    } else
        return '?';
    for (unsigned i = 1; i <= count; i++) {
        if ((p[i] & 0xc0) != 0x80)
            return '?';
        value = (value << 6) | (p[i] & 63);
    }
    *cursor += count;
    return value < minimum || value > 0x10ffff ||
                   (value >= 0xd800 && value <= 0xdfff)
               ? '?'
               : value;
}

static inline unsigned char text_next(const char **cursor) {
    unsigned c = text_codepoint(cursor);
    const char *next = *cursor;
    unsigned accent = text_codepoint(&next);
    const char *bases = NULL, *codes = NULL;
    switch (accent) {
    case 0x300:
        bases = "AEIOUaeiou";
        codes = "\xc0\xc8\xcc\xd2\xd9\xe0\xe8\xec\xf2\xf9";
        break;
    case 0x301:
        bases = "AEIOUYaeiouy";
        codes = "\xc1\xc9\xcd\xd3\xda\xdd\xe1\xe9\xed\xf3\xfa\xfd";
        break;
    case 0x302:
        bases = "AEIOUaeiou";
        codes = "\xc2\xca\xce\xd4\xdb\xe2\xea\xee\xf4\xfb";
        break;
    case 0x303:
        bases = "ANOano";
        codes = "\xc3\xd1\xd5\xe3\xf1\xf5";
        break;
    case 0x308:
        bases = "AEIOUaeiouy";
        codes = "\xc4\xcb\xcf\xd6\xdc\xe4\xeb\xef\xf6\xfc\xff";
        break;
    case 0x327:
        bases = "Cc";
        codes = "\xc7\xe7";
        break;
    }
    const char *match = bases && c && c < 128 ? strchr(bases, (int)c) : NULL;
    if (match) {
        c = (unsigned char)codes[match - bases];
        *cursor = next;
    }
    if (c == 0x152)
        return 128;
    if (c == 0x153)
        return 129;
    if (c == 0xa0)
        return ' ';
    if (c == 0xab || c == 0xbb || c == 0x201c || c == 0x201d)
        return '"';
    if (c == 0x2018 || c == 0x2019)
        return '\'';
    if (c == 0x2013 || c == 0x2014)
        return '-';
    if (c == '\n' || (c >= 32 && c < 127) || (c >= 0xc0 && c <= 0xff))
        return (unsigned char)c;
    return c < 32 ? ' ' : '?';
}

/* U+00C0..U+00FF: public-domain font8x8 by Daniel Hepper, based on IBM VGA
 * fonts. */
static const unsigned char text_latin[64][8] = {
    {7, 0, 28, 54, 99, 127, 99, 0},       {112, 0, 28, 54, 99, 127, 99, 0},
    {28, 54, 0, 62, 99, 127, 99, 0},      {110, 59, 0, 62, 99, 127, 99, 0},
    {99, 28, 54, 99, 127, 99, 99, 0},     {12, 12, 0, 30, 51, 63, 51, 0},
    {124, 54, 51, 127, 51, 51, 115, 0},   {30, 51, 3, 51, 30, 24, 48, 30},
    {7, 0, 63, 6, 30, 6, 63, 0},          {56, 0, 63, 6, 30, 6, 63, 0},
    {12, 18, 63, 6, 30, 6, 63, 0},        {54, 0, 63, 6, 30, 6, 63, 0},
    {7, 0, 30, 12, 12, 12, 30, 0},        {56, 0, 30, 12, 12, 12, 30, 0},
    {12, 18, 0, 30, 12, 12, 30, 0},       {51, 0, 30, 12, 12, 12, 30, 0},
    {63, 102, 111, 111, 102, 102, 63, 0}, {63, 0, 51, 55, 63, 59, 51, 0},
    {14, 0, 24, 60, 102, 60, 24, 0},      {112, 0, 24, 60, 102, 60, 24, 0},
    {60, 102, 24, 60, 102, 60, 24, 0},    {110, 59, 0, 62, 99, 99, 62, 0},
    {195, 24, 60, 102, 102, 60, 24, 0},   {0, 54, 28, 8, 28, 54, 0, 0},
    {92, 54, 115, 123, 111, 54, 29, 0},   {14, 0, 102, 102, 102, 102, 60, 0},
    {112, 0, 102, 102, 102, 102, 60, 0},  {60, 102, 0, 102, 102, 102, 60, 0},
    {51, 0, 51, 51, 51, 51, 30, 0},       {112, 0, 102, 102, 60, 24, 24, 0},
    {15, 6, 62, 102, 102, 62, 6, 15},     {0, 30, 51, 31, 51, 31, 3, 3},
    {7, 0, 30, 48, 62, 51, 126, 0},       {56, 0, 30, 48, 62, 51, 126, 0},
    {126, 195, 60, 96, 124, 102, 252, 0}, {110, 59, 30, 48, 62, 51, 126, 0},
    {51, 0, 30, 48, 62, 51, 126, 0},      {12, 12, 30, 48, 62, 51, 126, 0},
    {0, 0, 254, 48, 254, 51, 254, 0},     {0, 0, 30, 3, 3, 30, 48, 28},
    {7, 0, 30, 51, 63, 3, 30, 0},         {56, 0, 30, 51, 63, 3, 30, 0},
    {126, 195, 60, 102, 126, 6, 60, 0},   {51, 0, 30, 51, 63, 3, 30, 0},
    {7, 0, 14, 12, 12, 12, 30, 0},        {28, 0, 14, 12, 12, 12, 30, 0},
    {62, 99, 28, 24, 24, 24, 60, 0},      {51, 0, 14, 12, 12, 12, 30, 0},
    {27, 14, 27, 48, 62, 51, 30, 0},      {0, 31, 0, 31, 51, 51, 51, 0},
    {0, 7, 0, 30, 51, 51, 30, 0},         {0, 56, 0, 30, 51, 51, 30, 0},
    {30, 51, 0, 30, 51, 51, 30, 0},       {110, 59, 0, 30, 51, 51, 30, 0},
    {0, 51, 0, 30, 51, 51, 30, 0},        {24, 24, 0, 126, 0, 24, 24, 0},
    {0, 96, 60, 118, 126, 110, 60, 6},    {0, 7, 0, 51, 51, 51, 126, 0},
    {0, 56, 0, 51, 51, 51, 126, 0},       {30, 51, 0, 51, 51, 51, 126, 0},
    {0, 51, 0, 51, 51, 51, 126, 0},       {0, 56, 0, 51, 51, 62, 48, 31},
    {0, 0, 6, 62, 102, 62, 6, 0},         {0, 51, 0, 51, 51, 62, 48, 31}};

static inline void text_extend_font(unsigned char *output,
                                    const unsigned char *ascii) {
    memset(output, 0, TEXT_GLYPHS * TEXT_GLYPH_BYTES);
    memcpy(output, ascii, 96 * TEXT_GLYPH_BYTES);
    memcpy(output + (192 - TEXT_FIRST_GLYPH) * TEXT_GLYPH_BYTES, text_latin,
           sizeof(text_latin));
    const unsigned char ligatures[2][8] = {
        {0x7e, 0x1b, 0x1b, 0x7b, 0x1b, 0x1b, 0x7e, 0},
        {0, 0, 0x7e, 0xdb, 0xfb, 0x1b, 0x7e, 0}};
    memcpy(output + (128 - TEXT_FIRST_GLYPH) * TEXT_GLYPH_BYTES, ligatures,
           sizeof(ligatures));
}
#endif
