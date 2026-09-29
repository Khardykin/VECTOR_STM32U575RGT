/*******************************************************************************
 * Size: 11 px
 * Bpp: 2
 * Opts: --bpp 2 --size 11 --font C:/Users/Dmitriy/SquareLine/examples/AvisX5Pro/assets/fonts/ofont.ru_Roboto.ttf -o C:/Users/Dmitriy/SquareLine/examples/AvisX5Pro/assets/fonts\ui_font_number11.c --format lvgl -r 0x20-0x7F -r 0xB0 -r 0x2022 -r 0x410-0x44f -r 0x451 --no-compress --no-prefilter
 ******************************************************************************/

#include "../ui.h"

#ifndef UI_FONT_NUMBER11
#define UI_FONT_NUMBER11 1
#endif

#if UI_FONT_NUMBER11

/*-----------------
 *    BITMAPS
 *----------------*/

/*Store the image of the glyphs*/
static LV_ATTRIBUTE_LARGE_CONST const uint8_t glyph_bitmap[] = {
    /* U+0020 " " */

    /* U+0021 "!" */
    0x33, 0x33, 0x31, 0x3,

    /* U+0022 "\"" */
    0xac, 0x9c, 0x48,

    /* U+0023 "#" */
    0x5, 0x20, 0x21, 0x43, 0xff, 0x43, 0x30, 0x8,
    0xc1, 0xff, 0x81, 0x58, 0x8, 0x90,

    /* U+0024 "$" */
    0x0, 0x0, 0x20, 0x1f, 0x83, 0xd, 0x30, 0x52,
    0xd0, 0x2, 0xc1, 0xa, 0x30, 0x92, 0xf8, 0x6,
    0x0,

    /* U+0025 "%" */
    0x29, 0x0, 0x52, 0x20, 0x52, 0x60, 0x29, 0x80,
    0x2, 0x68, 0x2, 0x86, 0x8, 0x86, 0x0, 0x28,

    /* U+0026 "&" */
    0x1f, 0x40, 0xc3, 0x3, 0x58, 0x7, 0x80, 0x3a,
    0x12, 0x8a, 0x8a, 0xf, 0xa, 0xec,

    /* U+0027 "'" */
    0xa9, 0x40,

    /* U+0028 "(" */
    0x1, 0xc, 0x24, 0x30, 0x30, 0x70, 0x30, 0x30,
    0x30, 0x18, 0x8, 0x0,

    /* U+0029 ")" */
    0x81, 0x83, 0x6, 0xc, 0x30, 0xc6, 0x24, 0xc9,
    0x10,

    /* U+002A "*" */
    0xc, 0x17, 0x52, 0xe0, 0xe8, 0x11, 0x0,

    /* U+002B "+" */
    0x5, 0x0, 0x60, 0x6, 0xb, 0xff, 0x6, 0x0,
    0x60,

    /* U+002C "," */
    0x16, 0x80,

    /* U+002D "-" */
    0xb8,

    /* U+002E "." */
    0x3,

    /* U+002F "/" */
    0x3, 0x0, 0x80, 0x90, 0x30, 0x18, 0xc, 0x2,
    0x2, 0x40, 0x40, 0x0,

    /* U+0030 "0" */
    0x1f, 0x83, 0xd, 0x60, 0xa6, 0x6, 0x60, 0x66,
    0xa, 0x30, 0xd1, 0xf8,

    /* U+0031 "1" */
    0x3, 0xe1, 0x86, 0x18, 0x61, 0x86, 0x18,

    /* U+0032 "2" */
    0x1f, 0x87, 0xd, 0x0, 0x90, 0xc, 0x3, 0x40,
    0xd0, 0x28, 0x7, 0xfe,

    /* U+0033 "3" */
    0x2f, 0x86, 0xd, 0x0, 0xc0, 0xb8, 0x0, 0xc0,
    0x9, 0x70, 0xc2, 0xf4,

    /* U+0034 "4" */
    0x2, 0xc0, 0x3c, 0xd, 0xc2, 0x4c, 0x30, 0xcb,
    0xff, 0x0, 0xc0, 0xc,

    /* U+0035 "5" */
    0x2f, 0xd3, 0x0, 0x30, 0x3, 0xf8, 0x20, 0xd0,
    0x6, 0x30, 0x91, 0xf8,

    /* U+0036 "6" */
    0xb, 0xc3, 0x40, 0x30, 0x7, 0xb8, 0x70, 0xa3,
    0x7, 0x30, 0xa0, 0xf8,

    /* U+0037 "7" */
    0xbf, 0xe0, 0xc, 0x1, 0x80, 0x30, 0x6, 0x0,
    0x90, 0xc, 0x0, 0xc0,

    /* U+0038 "8" */
    0x1f, 0x83, 0xd, 0x30, 0xd1, 0xf8, 0x30, 0xd6,
    0x6, 0x70, 0x91, 0xf8,

    /* U+0039 "9" */
    0x2f, 0x47, 0xc, 0xa0, 0x97, 0xe, 0x2f, 0x90,
    0x9, 0x0, 0xc2, 0xf4,

    /* U+003A ":" */
    0x30, 0x0, 0x3,

    /* U+003B ";" */
    0x30, 0x0, 0x23, 0x50,

    /* U+003C "<" */
    0x0, 0x1, 0xe7, 0x81, 0xe0, 0x7, 0x80, 0x10,

    /* U+003D "=" */
    0x3f, 0xd0, 0x0, 0x3f, 0xd0,

    /* U+003E ">" */
    0x0, 0x3, 0x80, 0x7, 0x80, 0x6c, 0x39, 0x0,
    0x0,

    /* U+003F "?" */
    0x3f, 0x28, 0x60, 0x18, 0xc, 0xd, 0x3, 0x0,
    0x0, 0x30,

    /* U+0040 "@" */
    0x2, 0xa9, 0x0, 0x80, 0x24, 0x20, 0xa4, 0xc2,
    0x24, 0xc8, 0x63, 0x8, 0x55, 0x30, 0x84, 0x63,
    0x18, 0x83, 0x29, 0xe4, 0x18, 0x0, 0x0, 0x6a,
    0x40,

    /* U+0041 "A" */
    0x3, 0x0, 0x7, 0x80, 0xc, 0xc0, 0xc, 0x90,
    0x24, 0x60, 0x3f, 0xf0, 0x60, 0x24, 0x90, 0xc,

    /* U+0042 "B" */
    0x3f, 0xd0, 0xc0, 0xc3, 0x3, 0xf, 0xf4, 0x30,
    0x30, 0xc0, 0x93, 0x3, 0xf, 0xf4,

    /* U+0043 "C" */
    0xb, 0xd0, 0xd0, 0xc3, 0x1, 0x58, 0x0, 0x60,
    0x0, 0xc0, 0x53, 0x43, 0x2, 0xf4,

    /* U+0044 "D" */
    0x3f, 0xd0, 0xc0, 0xc3, 0x1, 0x8c, 0x6, 0x30,
    0x18, 0xc0, 0x63, 0x3, 0xf, 0xf4,

    /* U+0045 "E" */
    0x3f, 0xf0, 0xc0, 0x3, 0x0, 0xf, 0xf8, 0x30,
    0x0, 0xc0, 0x3, 0x0, 0xf, 0xfc,

    /* U+0046 "F" */
    0x3f, 0xf0, 0xc0, 0x3, 0x0, 0xc, 0x0, 0x3f,
    0xe0, 0xc0, 0x3, 0x0, 0xc, 0x0,

    /* U+0047 "G" */
    0xb, 0xd0, 0xd0, 0xd3, 0x0, 0x18, 0x0, 0x60,
    0xf8, 0xc0, 0x63, 0x42, 0x82, 0xf8,

    /* U+0048 "H" */
    0x30, 0xc, 0xc0, 0x33, 0x0, 0xcc, 0x3, 0x3f,
    0xfc, 0xc0, 0x33, 0x0, 0xcc, 0x3,

    /* U+0049 "I" */
    0xcc, 0xcc, 0xcc, 0xcc,

    /* U+004A "J" */
    0x0, 0xc0, 0xc, 0x0, 0xc0, 0xc, 0x0, 0xc0,
    0xc, 0xa0, 0xc2, 0xf4,

    /* U+004B "K" */
    0x30, 0x34, 0x30, 0xa0, 0x32, 0x80, 0x3f, 0x0,
    0x32, 0x80, 0x30, 0xd0, 0x30, 0x70, 0x30, 0x28,

    /* U+004C "L" */
    0x30, 0x3, 0x0, 0x30, 0x3, 0x0, 0x30, 0x3,
    0x0, 0x30, 0x3, 0xfe,

    /* U+004D "M" */
    0x38, 0x3, 0x8f, 0x0, 0xe3, 0x90, 0xa8, 0xd8,
    0x36, 0x33, 0x19, 0x8c, 0x9c, 0x63, 0xf, 0x18,
    0xc2, 0x46,

    /* U+004E "N" */
    0x34, 0xc, 0xf0, 0x33, 0xa0, 0xcc, 0xc3, 0x31,
    0xcc, 0xc2, 0xb3, 0x3, 0xcc, 0x7,

    /* U+004F "O" */
    0xb, 0xd0, 0xd0, 0xd7, 0x1, 0xd8, 0x3, 0x60,
    0xd, 0xc0, 0x73, 0x43, 0x42, 0xf4,

    /* U+0050 "P" */
    0x3f, 0xe0, 0xc0, 0xd3, 0x2, 0x8c, 0xd, 0x3f,
    0xd0, 0xc0, 0x3, 0x0, 0xc, 0x0,

    /* U+0051 "Q" */
    0xb, 0xd0, 0x34, 0x34, 0x70, 0x1c, 0x60, 0xc,
    0x60, 0xc, 0x70, 0x1c, 0x34, 0x34, 0xb, 0xf0,
    0x0, 0x1c, 0x0, 0x0,

    /* U+0052 "R" */
    0x3f, 0xd0, 0xc0, 0xc3, 0x2, 0x4c, 0xc, 0x3f,
    0xe0, 0xc0, 0xd3, 0x2, 0x4c, 0xa,

    /* U+0053 "S" */
    0x1f, 0xc0, 0xc0, 0xc3, 0x1, 0xb, 0x80, 0x1,
    0xe0, 0x0, 0xc3, 0x3, 0x7, 0xf4,

    /* U+0054 "T" */
    0xbf, 0xf0, 0x1c, 0x0, 0x70, 0x1, 0xc0, 0x7,
    0x0, 0x1c, 0x0, 0x70, 0x1, 0xc0,

    /* U+0055 "U" */
    0x30, 0x18, 0xc0, 0x63, 0x1, 0x8c, 0x6, 0x30,
    0x18, 0xc0, 0x63, 0x43, 0x43, 0xf8,

    /* U+0056 "V" */
    0x90, 0x19, 0xc0, 0xd3, 0x3, 0x9, 0x18, 0xc,
    0xc0, 0x37, 0x0, 0xb4, 0x0, 0xc0,

    /* U+0057 "W" */
    0x90, 0xd0, 0xc6, 0xe, 0xc, 0x31, 0xb1, 0xc3,
    0x26, 0x58, 0x27, 0x1a, 0x41, 0xb0, 0xf0, 0xe,
    0xf, 0x0, 0xd0, 0xa0,

    /* U+0058 "X" */
    0x70, 0x34, 0xa2, 0x80, 0xdc, 0x1, 0xd0, 0x7,
    0x40, 0x37, 0x2, 0x86, 0x1c, 0xd,

    /* U+0059 "Y" */
    0xa0, 0x34, 0xc1, 0xc2, 0x8d, 0x3, 0x70, 0xb,
    0x0, 0xc, 0x0, 0x30, 0x0, 0xc0,

    /* U+005A "Z" */
    0x7f, 0xf0, 0x2, 0x40, 0x1c, 0x0, 0xc0, 0x9,
    0x0, 0x70, 0x3, 0x0, 0x1f, 0xfc,

    /* U+005B "[" */
    0x38, 0xc3, 0xc, 0x30, 0xc3, 0xc, 0x30, 0xc3,
    0x80,

    /* U+005C "\\" */
    0x90, 0xc, 0x3, 0x0, 0x60, 0xc, 0x2, 0x40,
    0x60, 0xc, 0x1, 0x0,

    /* U+005D "]" */
    0xf0, 0xc3, 0xc, 0x30, 0xc3, 0xc, 0x30, 0xcf,
    0x0,

    /* U+005E "^" */
    0x1c, 0xb, 0x43, 0x61, 0x8c,

    /* U+005F "_" */
    0xff, 0xc0,

    /* U+0060 "`" */
    0x10, 0x90,

    /* U+0061 "a" */
    0x1f, 0x82, 0xc, 0x1a, 0xd7, 0xd, 0x70, 0xd3,
    0xed,

    /* U+0062 "b" */
    0x30, 0x3, 0x0, 0x3b, 0x83, 0xd, 0x30, 0x63,
    0x6, 0x30, 0x93, 0xb8,

    /* U+0063 "c" */
    0x1f, 0x83, 0xd, 0x60, 0x6, 0x0, 0x30, 0x91,
    0xf8,

    /* U+0064 "d" */
    0x0, 0x90, 0x9, 0x1f, 0xd3, 0xd, 0x60, 0x96,
    0x9, 0x30, 0x92, 0xe9,

    /* U+0065 "e" */
    0x1f, 0x83, 0xc, 0x7f, 0xd6, 0x0, 0x30, 0x1,
    0xf8,

    /* U+0066 "f" */
    0x1e, 0x34, 0xbd, 0x34, 0x34, 0x34, 0x34, 0x34,

    /* U+0067 "g" */
    0x1f, 0x93, 0xd, 0x60, 0x96, 0x9, 0x30, 0xd1,
    0xfd, 0x0, 0xd2, 0xf8,

    /* U+0068 "h" */
    0x30, 0x3, 0x0, 0x3b, 0x83, 0xd, 0x30, 0x93,
    0xa, 0x30, 0xa3, 0xa,

    /* U+0069 "i" */
    0x30, 0x33, 0x33, 0x33,

    /* U+006A "j" */
    0xc, 0x0, 0xc3, 0xc, 0x30, 0xc3, 0xd, 0xd0,

    /* U+006B "k" */
    0x30, 0x3, 0x0, 0x31, 0xc3, 0x30, 0x3e, 0x3,
    0x30, 0x32, 0x83, 0xd,

    /* U+006C "l" */
    0x33, 0x33, 0x33, 0x33,

    /* U+006D "m" */
    0x3b, 0x9f, 0xc, 0x38, 0x63, 0x9, 0xc, 0xc2,
    0x43, 0x30, 0x90, 0xcc, 0x24, 0x30,

    /* U+006E "n" */
    0x3b, 0x83, 0xd, 0x30, 0x93, 0x9, 0x30, 0x93,
    0x9,

    /* U+006F "o" */
    0x1f, 0x83, 0xd, 0x60, 0x66, 0x6, 0x30, 0x91,
    0xf8,

    /* U+0070 "p" */
    0x3b, 0x83, 0x9, 0x30, 0x63, 0x6, 0x30, 0x93,
    0xb8, 0x30, 0x3, 0x0,

    /* U+0071 "q" */
    0x1f, 0x93, 0xd, 0x60, 0x96, 0x9, 0x30, 0xd2,
    0xfd, 0x0, 0x90, 0x9,

    /* U+0072 "r" */
    0x3a, 0x30, 0x30, 0x30, 0x30, 0x30,

    /* U+0073 "s" */
    0x2f, 0x43, 0xc, 0x39, 0x0, 0x6c, 0x60, 0xc2,
    0xf8,

    /* U+0074 "t" */
    0x10, 0x30, 0xbc, 0x30, 0x30, 0x30, 0x30, 0x2c,

    /* U+0075 "u" */
    0x70, 0x97, 0x9, 0x70, 0x93, 0x9, 0x30, 0xd2,
    0xf9,

    /* U+0076 "v" */
    0x90, 0xc3, 0xc, 0x32, 0x41, 0x70, 0xe, 0x0,
    0xd0,

    /* U+0077 "w" */
    0x91, 0x83, 0x18, 0xb1, 0x83, 0x39, 0x90, 0xe9,
    0xb0, 0x2c, 0x3c, 0x3, 0xa, 0x0,

    /* U+0078 "x" */
    0x71, 0xc2, 0x74, 0xe, 0x0, 0xe0, 0x37, 0x47,
    0xc,

    /* U+0079 "y" */
    0x90, 0xc7, 0x1c, 0x32, 0x42, 0xb0, 0xe, 0x0,
    0xd0, 0xc, 0x7, 0x40,

    /* U+007A "z" */
    0x7f, 0xc0, 0x34, 0xa, 0x1, 0xc0, 0x30, 0x7,
    0xfc,

    /* U+007B "{" */
    0x5, 0x1c, 0x28, 0x28, 0x24, 0xb0, 0x24, 0x28,
    0x18, 0x1c, 0x5,

    /* U+007C "|" */
    0x33, 0x33, 0x33, 0x33, 0x31,

    /* U+007D "}" */
    0x80, 0x30, 0x30, 0x34, 0x24, 0xd, 0x24, 0x34,
    0x30, 0x30, 0x80,

    /* U+007E "~" */
    0x2e, 0x4, 0x47, 0xd0,

    /* U+00B0 "°" */
    0x28, 0x55, 0x28,

    /* U+0410 "А" */
    0x3, 0x0, 0x7, 0x80, 0xc, 0xc0, 0xc, 0x90,
    0x24, 0x60, 0x3f, 0xf0, 0x60, 0x24, 0x90, 0xc,

    /* U+0411 "Б" */
    0x3f, 0xe0, 0xc0, 0x3, 0x0, 0xf, 0xf4, 0x30,
    0x30, 0xc0, 0x93, 0x3, 0x4f, 0xf4,

    /* U+0412 "В" */
    0x3f, 0xd0, 0xc0, 0xc3, 0x3, 0xf, 0xf4, 0x30,
    0x30, 0xc0, 0x93, 0x3, 0xf, 0xf4,

    /* U+0413 "Г" */
    0x3f, 0xe3, 0x0, 0x30, 0x3, 0x0, 0x30, 0x3,
    0x0, 0x30, 0x3, 0x0,

    /* U+0414 "Д" */
    0xf, 0xfc, 0x3, 0x3, 0x0, 0xc0, 0xc0, 0x30,
    0x30, 0xc, 0xc, 0x7, 0x3, 0x2, 0x40, 0xc2,
    0xff, 0xfc, 0x90, 0x3, 0x24, 0x0, 0xc0,

    /* U+0415 "Е" */
    0x3f, 0xf0, 0xc0, 0x3, 0x0, 0xf, 0xf8, 0x30,
    0x0, 0xc0, 0x3, 0x0, 0xf, 0xfc,

    /* U+0416 "Ж" */
    0x70, 0x60, 0xd2, 0x86, 0x28, 0xd, 0x63, 0x0,
    0x7f, 0xd0, 0xd, 0x67, 0x1, 0xc6, 0x34, 0x34,
    0x60, 0xca, 0x6, 0xa,

    /* U+0417 "З" */
    0x1f, 0xe0, 0xc0, 0x90, 0x2, 0x41, 0xf8, 0x0,
    0x24, 0x40, 0x63, 0x2, 0x47, 0xf8,

    /* U+0418 "И" */
    0x30, 0x1c, 0xc0, 0xf3, 0xa, 0xcc, 0x73, 0x33,
    0xc, 0xe8, 0x33, 0xc0, 0xcd, 0x3,

    /* U+0419 "Й" */
    0x6, 0x80, 0x0, 0x3, 0x1, 0xcc, 0xf, 0x30,
    0xac, 0xc7, 0x33, 0x30, 0xce, 0x83, 0x3c, 0xc,
    0xd0, 0x30,

    /* U+041A "К" */
    0x30, 0x34, 0x30, 0xa0, 0x32, 0x80, 0x3f, 0x0,
    0x32, 0x80, 0x30, 0xd0, 0x30, 0x70, 0x30, 0x28,

    /* U+041B "Л" */
    0xf, 0xfc, 0x30, 0x30, 0xc0, 0xc3, 0x3, 0xc,
    0xc, 0x30, 0x32, 0x80, 0xec, 0x3,

    /* U+041C "М" */
    0x38, 0x3, 0x8f, 0x0, 0xe3, 0x90, 0xa8, 0xd8,
    0x36, 0x33, 0x19, 0x8c, 0x9c, 0x63, 0xf, 0x18,
    0xc2, 0x46,

    /* U+041D "Н" */
    0x30, 0xc, 0xc0, 0x33, 0x0, 0xcc, 0x3, 0x3f,
    0xfc, 0xc0, 0x33, 0x0, 0xcc, 0x3,

    /* U+041E "О" */
    0xb, 0xd0, 0xd0, 0xd7, 0x1, 0xd8, 0x3, 0x60,
    0xd, 0xc0, 0x73, 0x43, 0x42, 0xf4,

    /* U+041F "П" */
    0x3f, 0xfc, 0xc0, 0x33, 0x0, 0xcc, 0x3, 0x30,
    0xc, 0xc0, 0x33, 0x0, 0xcc, 0x3,

    /* U+0420 "Р" */
    0x3f, 0xe0, 0xc0, 0xd3, 0x2, 0x8c, 0xd, 0x3f,
    0xd0, 0xc0, 0x3, 0x0, 0xc, 0x0,

    /* U+0421 "С" */
    0xb, 0xd0, 0xd0, 0xc3, 0x1, 0x58, 0x0, 0x60,
    0x0, 0xc0, 0x53, 0x43, 0x2, 0xf4,

    /* U+0422 "Т" */
    0xbf, 0xf0, 0x1c, 0x0, 0x70, 0x1, 0xc0, 0x7,
    0x0, 0x1c, 0x0, 0x70, 0x1, 0xc0,

    /* U+0423 "У" */
    0x70, 0x34, 0xd0, 0xc1, 0x8a, 0x3, 0x30, 0xb,
    0x80, 0xd, 0x0, 0x30, 0x7, 0x40,

    /* U+0424 "Ф" */
    0x0, 0x40, 0x2, 0xfd, 0x3, 0x4c, 0xa1, 0x83,
    0xc, 0xa0, 0xc3, 0x18, 0x30, 0xc3, 0x4c, 0xa0,
    0x2f, 0xd0, 0x0, 0xc0, 0x0,

    /* U+0425 "Х" */
    0x70, 0x34, 0xa2, 0x80, 0xdc, 0x1, 0xd0, 0x7,
    0x40, 0x37, 0x2, 0x86, 0x1c, 0xd,

    /* U+0426 "Ц" */
    0x30, 0xc, 0xc, 0x3, 0x3, 0x0, 0xc0, 0xc0,
    0x30, 0x30, 0xc, 0xc, 0x3, 0x3, 0x0, 0xc0,
    0xff, 0xfc, 0x0, 0x3, 0x0, 0x0, 0xc0,

    /* U+0427 "Ч" */
    0x30, 0x18, 0xc0, 0x63, 0x1, 0x8c, 0xa, 0x1f,
    0xf8, 0x0, 0x60, 0x1, 0x80, 0x6,

    /* U+0428 "Ш" */
    0x30, 0x60, 0x93, 0x6, 0x9, 0x30, 0x60, 0x93,
    0x6, 0x9, 0x30, 0x60, 0x93, 0x6, 0x9, 0x30,
    0x60, 0x93, 0xff, 0xfd,

    /* U+0429 "Щ" */
    0x30, 0x60, 0x90, 0xc1, 0x82, 0x43, 0x6, 0x9,
    0xc, 0x18, 0x24, 0x30, 0x60, 0x90, 0xc1, 0x82,
    0x43, 0x6, 0x9, 0xf, 0xff, 0xfc, 0x0, 0x0,
    0x30, 0x0, 0x0, 0x80,

    /* U+042A "Ъ" */
    0xbd, 0x0, 0xd, 0x0, 0xd, 0x0, 0xf, 0xf8,
    0xd, 0xa, 0xd, 0x3, 0xd, 0xa, 0xf, 0xf8,

    /* U+042B "Ы" */
    0x30, 0x1, 0x8c, 0x0, 0x63, 0x0, 0x18, 0xff,
    0x46, 0x30, 0x35, 0x8c, 0xa, 0x63, 0x3, 0x58,
    0xff, 0x86,

    /* U+042C "Ь" */
    0x30, 0x0, 0xc0, 0x3, 0x0, 0xf, 0xf4, 0x30,
    0x34, 0xc0, 0x93, 0x3, 0x4f, 0xf4,

    /* U+042D "Э" */
    0xb, 0xd0, 0xd0, 0x91, 0x1, 0x80, 0xff, 0x0,
    0xc, 0xc0, 0x62, 0x43, 0x42, 0xf4,

    /* U+042E "Ю" */
    0xc1, 0xf8, 0x30, 0xc0, 0xcc, 0x90, 0x27, 0x34,
    0x9, 0xfd, 0x2, 0x72, 0x40, 0x9c, 0x70, 0x73,
    0x7, 0xe0,

    /* U+042F "Я" */
    0x1f, 0xf0, 0xc0, 0xc3, 0x3, 0xd, 0xc, 0xf,
    0xf0, 0x30, 0xc2, 0x43, 0xc, 0xc,

    /* U+0430 "а" */
    0x1f, 0x82, 0xc, 0x1a, 0xd7, 0xd, 0x70, 0xd3,
    0xed,

    /* U+0431 "б" */
    0x0, 0x40, 0x6c, 0x24, 0x3, 0xb8, 0x70, 0xd6,
    0x6, 0x60, 0x63, 0x9, 0x1f, 0x80,

    /* U+0432 "в" */
    0x3f, 0x83, 0x9, 0x30, 0x93, 0xfc, 0x30, 0xa3,
    0xfd,

    /* U+0433 "г" */
    0x3f, 0x4c, 0x3, 0x0, 0xc0, 0x30, 0xc, 0x0,

    /* U+0434 "д" */
    0xf, 0xe0, 0x71, 0x81, 0x86, 0x6, 0x18, 0x30,
    0x62, 0xff, 0xd9, 0x2, 0x50, 0x8,

    /* U+0435 "е" */
    0x1f, 0x83, 0xc, 0x7f, 0xd6, 0x0, 0x30, 0x1,
    0xf8,

    /* U+0436 "ж" */
    0x71, 0x8a, 0xa, 0x63, 0x0, 0xff, 0x40, 0x66,
    0x70, 0x31, 0x8d, 0x28, 0x61, 0xc0,

    /* U+0437 "з" */
    0x2f, 0x41, 0x1c, 0xf, 0x40, 0xc, 0x60, 0xc2,
    0xf4,

    /* U+0438 "и" */
    0x30, 0xd3, 0x1d, 0x33, 0x93, 0x99, 0x38, 0x93,
    0x9,

    /* U+0439 "й" */
    0x0, 0x40, 0xa0, 0x0, 0x3, 0xd, 0x31, 0xd3,
    0x39, 0x39, 0x93, 0x89, 0x30, 0x90,

    /* U+043A "к" */
    0x30, 0xd3, 0x28, 0x33, 0x3, 0xe0, 0x33, 0x43,
    0xd,

    /* U+043B "л" */
    0x2f, 0xd2, 0x49, 0x24, 0x92, 0x49, 0x30, 0x9e,
    0x9,

    /* U+043C "м" */
    0x34, 0xd, 0x3c, 0x2d, 0x3c, 0x39, 0x36, 0x69,
    0x33, 0xc9, 0x31, 0x89,

    /* U+043D "н" */
    0x30, 0x93, 0x9, 0x30, 0x93, 0xfd, 0x30, 0x93,
    0x9,

    /* U+043E "о" */
    0x1f, 0x83, 0xd, 0x60, 0x66, 0x6, 0x30, 0x91,
    0xf8,

    /* U+043F "п" */
    0x3f, 0xd3, 0x9, 0x30, 0x93, 0x9, 0x30, 0x93,
    0x9,

    /* U+0440 "р" */
    0x3b, 0x83, 0x9, 0x30, 0x63, 0x6, 0x30, 0x93,
    0xb8, 0x30, 0x3, 0x0,

    /* U+0441 "с" */
    0x1f, 0x83, 0xd, 0x60, 0x6, 0x0, 0x30, 0x91,
    0xf8,

    /* U+0442 "т" */
    0xbf, 0xc0, 0x90, 0x9, 0x0, 0x90, 0x9, 0x0,
    0x90,

    /* U+0443 "у" */
    0x90, 0xc7, 0x1c, 0x32, 0x42, 0xb0, 0xe, 0x0,
    0xd0, 0xc, 0x7, 0x40,

    /* U+0444 "ф" */
    0x2, 0x40, 0x2, 0x40, 0x2, 0x40, 0x2f, 0xf8,
    0x32, 0x4c, 0x62, 0x49, 0x62, 0x49, 0x32, 0x4c,
    0x2f, 0xf8, 0x2, 0x40, 0x2, 0x40,

    /* U+0445 "х" */
    0x71, 0xc2, 0x74, 0xe, 0x0, 0xe0, 0x37, 0x47,
    0xc,

    /* U+0446 "ц" */
    0x30, 0x90, 0xc2, 0x43, 0x9, 0xc, 0x24, 0x30,
    0x90, 0xff, 0xc0, 0x3, 0x0, 0xc,

    /* U+0447 "ч" */
    0x60, 0xd6, 0xd, 0x30, 0xd1, 0xfd, 0x0, 0xd0,
    0xd,

    /* U+0448 "ш" */
    0x30, 0xc3, 0xc, 0x30, 0xc3, 0xc, 0x30, 0xc3,
    0xc, 0x30, 0xc3, 0xf, 0xff, 0xc0,

    /* U+0449 "щ" */
    0x30, 0xc3, 0xc, 0x30, 0xc3, 0xc, 0x30, 0xc3,
    0xc, 0x30, 0xc3, 0xf, 0xff, 0xf0, 0x0, 0xc,
    0x0, 0x3,

    /* U+044A "ъ" */
    0xbc, 0x0, 0x30, 0x0, 0xfe, 0x3, 0x9, 0xc,
    0x24, 0x3f, 0x80,

    /* U+044B "ы" */
    0x30, 0x6, 0x30, 0x6, 0x3f, 0x86, 0x30, 0x96,
    0x30, 0xa6, 0x3f, 0x86,

    /* U+044C "ь" */
    0x30, 0x3, 0x0, 0x3f, 0x83, 0x9, 0x30, 0x93,
    0xf8,

    /* U+044D "э" */
    0x2f, 0x41, 0xc, 0xb, 0xd0, 0xd, 0x70, 0xc2,
    0xf4,

    /* U+044E "ю" */
    0x30, 0xbd, 0xc, 0xa0, 0xc3, 0xf4, 0x24, 0xc9,
    0x9, 0x31, 0x83, 0xc, 0x2f, 0x40,

    /* U+044F "я" */
    0x1f, 0xd3, 0x9, 0x30, 0x91, 0xfd, 0x28, 0x97,
    0x9,

    /* U+0451 "ё" */
    0x35, 0xc0, 0x0, 0x1f, 0x83, 0xc, 0x7f, 0xd6,
    0x0, 0x30, 0x1, 0xf8,

    /* U+2022 "•" */
    0x14, 0xf3, 0x80
};


/*---------------------
 *  GLYPH DESCRIPTION
 *--------------------*/

static const lv_font_fmt_txt_glyph_dsc_t glyph_dsc[] = {
    {.bitmap_index = 0, .adv_w = 0, .box_w = 0, .box_h = 0, .ofs_x = 0, .ofs_y = 0} /* id = 0 reserved */,
    {.bitmap_index = 0, .adv_w = 44, .box_w = 0, .box_h = 0, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 0, .adv_w = 46, .box_w = 2, .box_h = 8, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 4, .adv_w = 57, .box_w = 4, .box_h = 3, .ofs_x = 0, .ofs_y = 5},
    {.bitmap_index = 7, .adv_w = 110, .box_w = 7, .box_h = 8, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 21, .adv_w = 99, .box_w = 6, .box_h = 11, .ofs_x = 0, .ofs_y = -1},
    {.bitmap_index = 38, .adv_w = 129, .box_w = 8, .box_h = 8, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 54, .adv_w = 110, .box_w = 7, .box_h = 8, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 68, .adv_w = 31, .box_w = 2, .box_h = 3, .ofs_x = 0, .ofs_y = 5},
    {.bitmap_index = 70, .adv_w = 58, .box_w = 4, .box_h = 12, .ofs_x = 0, .ofs_y = -3},
    {.bitmap_index = 82, .adv_w = 59, .box_w = 3, .box_h = 12, .ofs_x = 0, .ofs_y = -3},
    {.bitmap_index = 91, .adv_w = 76, .box_w = 5, .box_h = 5, .ofs_x = 0, .ofs_y = 3},
    {.bitmap_index = 98, .adv_w = 100, .box_w = 6, .box_h = 6, .ofs_x = 0, .ofs_y = 1},
    {.bitmap_index = 107, .adv_w = 35, .box_w = 2, .box_h = 3, .ofs_x = 0, .ofs_y = -1},
    {.bitmap_index = 109, .adv_w = 48, .box_w = 3, .box_h = 1, .ofs_x = 0, .ofs_y = 3},
    {.bitmap_index = 110, .adv_w = 47, .box_w = 2, .box_h = 2, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 111, .adv_w = 73, .box_w = 5, .box_h = 9, .ofs_x = 0, .ofs_y = -1},
    {.bitmap_index = 123, .adv_w = 99, .box_w = 6, .box_h = 8, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 135, .adv_w = 99, .box_w = 3, .box_h = 9, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 142, .adv_w = 99, .box_w = 6, .box_h = 8, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 154, .adv_w = 99, .box_w = 6, .box_h = 8, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 166, .adv_w = 99, .box_w = 6, .box_h = 8, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 178, .adv_w = 99, .box_w = 6, .box_h = 8, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 190, .adv_w = 99, .box_w = 6, .box_h = 8, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 202, .adv_w = 99, .box_w = 6, .box_h = 8, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 214, .adv_w = 99, .box_w = 6, .box_h = 8, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 226, .adv_w = 99, .box_w = 6, .box_h = 8, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 238, .adv_w = 44, .box_w = 2, .box_h = 6, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 241, .adv_w = 45, .box_w = 2, .box_h = 7, .ofs_x = 0, .ofs_y = -1},
    {.bitmap_index = 245, .adv_w = 89, .box_w = 5, .box_h = 6, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 253, .adv_w = 99, .box_w = 6, .box_h = 3, .ofs_x = 0, .ofs_y = 2},
    {.bitmap_index = 258, .adv_w = 92, .box_w = 6, .box_h = 6, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 267, .adv_w = 84, .box_w = 5, .box_h = 8, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 277, .adv_w = 158, .box_w = 10, .box_h = 10, .ofs_x = 0, .ofs_y = -2},
    {.bitmap_index = 302, .adv_w = 114, .box_w = 8, .box_h = 8, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 318, .adv_w = 112, .box_w = 7, .box_h = 8, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 332, .adv_w = 111, .box_w = 7, .box_h = 8, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 346, .adv_w = 116, .box_w = 7, .box_h = 8, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 360, .adv_w = 103, .box_w = 7, .box_h = 8, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 374, .adv_w = 103, .box_w = 7, .box_h = 8, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 388, .adv_w = 120, .box_w = 7, .box_h = 8, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 402, .adv_w = 126, .box_w = 7, .box_h = 8, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 416, .adv_w = 50, .box_w = 2, .box_h = 8, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 420, .adv_w = 97, .box_w = 6, .box_h = 8, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 432, .adv_w = 113, .box_w = 8, .box_h = 8, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 448, .adv_w = 95, .box_w = 6, .box_h = 8, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 460, .adv_w = 155, .box_w = 9, .box_h = 8, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 478, .adv_w = 126, .box_w = 7, .box_h = 8, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 492, .adv_w = 120, .box_w = 7, .box_h = 8, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 506, .adv_w = 113, .box_w = 7, .box_h = 8, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 520, .adv_w = 120, .box_w = 8, .box_h = 10, .ofs_x = 0, .ofs_y = -2},
    {.bitmap_index = 540, .adv_w = 117, .box_w = 7, .box_h = 8, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 554, .adv_w = 110, .box_w = 7, .box_h = 8, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 568, .adv_w = 103, .box_w = 7, .box_h = 8, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 582, .adv_w = 119, .box_w = 7, .box_h = 8, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 596, .adv_w = 111, .box_w = 7, .box_h = 8, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 610, .adv_w = 155, .box_w = 10, .box_h = 8, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 630, .adv_w = 111, .box_w = 7, .box_h = 8, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 644, .adv_w = 107, .box_w = 7, .box_h = 8, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 658, .adv_w = 105, .box_w = 7, .box_h = 8, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 672, .adv_w = 47, .box_w = 3, .box_h = 11, .ofs_x = 0, .ofs_y = -2},
    {.bitmap_index = 681, .adv_w = 73, .box_w = 5, .box_h = 9, .ofs_x = 0, .ofs_y = -1},
    {.bitmap_index = 693, .adv_w = 47, .box_w = 3, .box_h = 11, .ofs_x = 0, .ofs_y = -2},
    {.bitmap_index = 702, .adv_w = 74, .box_w = 5, .box_h = 4, .ofs_x = 0, .ofs_y = 4},
    {.bitmap_index = 707, .adv_w = 80, .box_w = 5, .box_h = 1, .ofs_x = 0, .ofs_y = -1},
    {.bitmap_index = 709, .adv_w = 55, .box_w = 3, .box_h = 2, .ofs_x = 0, .ofs_y = 7},
    {.bitmap_index = 711, .adv_w = 97, .box_w = 6, .box_h = 6, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 720, .adv_w = 100, .box_w = 6, .box_h = 8, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 732, .adv_w = 93, .box_w = 6, .box_h = 6, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 741, .adv_w = 100, .box_w = 6, .box_h = 8, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 753, .adv_w = 93, .box_w = 6, .box_h = 6, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 762, .adv_w = 61, .box_w = 4, .box_h = 8, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 770, .adv_w = 100, .box_w = 6, .box_h = 8, .ofs_x = 0, .ofs_y = -2},
    {.bitmap_index = 782, .adv_w = 100, .box_w = 6, .box_h = 8, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 794, .adv_w = 44, .box_w = 2, .box_h = 8, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 798, .adv_w = 46, .box_w = 3, .box_h = 10, .ofs_x = -1, .ofs_y = -2},
    {.bitmap_index = 806, .adv_w = 90, .box_w = 6, .box_h = 8, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 818, .adv_w = 44, .box_w = 2, .box_h = 8, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 822, .adv_w = 154, .box_w = 9, .box_h = 6, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 836, .adv_w = 100, .box_w = 6, .box_h = 6, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 845, .adv_w = 100, .box_w = 6, .box_h = 6, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 854, .adv_w = 100, .box_w = 6, .box_h = 8, .ofs_x = 0, .ofs_y = -2},
    {.bitmap_index = 866, .adv_w = 100, .box_w = 6, .box_h = 8, .ofs_x = 0, .ofs_y = -2},
    {.bitmap_index = 878, .adv_w = 60, .box_w = 4, .box_h = 6, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 884, .adv_w = 92, .box_w = 6, .box_h = 6, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 893, .adv_w = 61, .box_w = 4, .box_h = 8, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 901, .adv_w = 100, .box_w = 6, .box_h = 6, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 910, .adv_w = 89, .box_w = 6, .box_h = 6, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 919, .adv_w = 133, .box_w = 9, .box_h = 6, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 933, .adv_w = 89, .box_w = 6, .box_h = 6, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 942, .adv_w = 89, .box_w = 6, .box_h = 8, .ofs_x = 0, .ofs_y = -2},
    {.bitmap_index = 954, .adv_w = 89, .box_w = 6, .box_h = 6, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 963, .adv_w = 60, .box_w = 4, .box_h = 11, .ofs_x = 0, .ofs_y = -2},
    {.bitmap_index = 974, .adv_w = 44, .box_w = 2, .box_h = 10, .ofs_x = 0, .ofs_y = -2},
    {.bitmap_index = 979, .adv_w = 60, .box_w = 4, .box_h = 11, .ofs_x = 0, .ofs_y = -2},
    {.bitmap_index = 990, .adv_w = 120, .box_w = 7, .box_h = 2, .ofs_x = 0, .ofs_y = 2},
    {.bitmap_index = 994, .adv_w = 66, .box_w = 4, .box_h = 3, .ofs_x = 0, .ofs_y = 5},
    {.bitmap_index = 997, .adv_w = 114, .box_w = 8, .box_h = 8, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 1013, .adv_w = 111, .box_w = 7, .box_h = 8, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 1027, .adv_w = 112, .box_w = 7, .box_h = 8, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 1041, .adv_w = 98, .box_w = 6, .box_h = 8, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 1053, .adv_w = 133, .box_w = 9, .box_h = 10, .ofs_x = 0, .ofs_y = -2},
    {.bitmap_index = 1076, .adv_w = 103, .box_w = 7, .box_h = 8, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 1090, .adv_w = 161, .box_w = 10, .box_h = 8, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 1110, .adv_w = 119, .box_w = 7, .box_h = 8, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 1124, .adv_w = 126, .box_w = 7, .box_h = 8, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 1138, .adv_w = 126, .box_w = 7, .box_h = 10, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 1156, .adv_w = 113, .box_w = 8, .box_h = 8, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 1172, .adv_w = 125, .box_w = 7, .box_h = 8, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 1186, .adv_w = 155, .box_w = 9, .box_h = 8, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 1204, .adv_w = 126, .box_w = 7, .box_h = 8, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 1218, .adv_w = 120, .box_w = 7, .box_h = 8, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 1232, .adv_w = 126, .box_w = 7, .box_h = 8, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 1246, .adv_w = 113, .box_w = 7, .box_h = 8, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 1260, .adv_w = 111, .box_w = 7, .box_h = 8, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 1274, .adv_w = 103, .box_w = 7, .box_h = 8, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 1288, .adv_w = 111, .box_w = 7, .box_h = 8, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 1302, .adv_w = 137, .box_w = 9, .box_h = 9, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 1323, .adv_w = 111, .box_w = 7, .box_h = 8, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 1337, .adv_w = 135, .box_w = 9, .box_h = 10, .ofs_x = 0, .ofs_y = -2},
    {.bitmap_index = 1360, .adv_w = 121, .box_w = 7, .box_h = 8, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 1374, .adv_w = 166, .box_w = 10, .box_h = 8, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 1394, .adv_w = 171, .box_w = 11, .box_h = 10, .ofs_x = 0, .ofs_y = -2},
    {.bitmap_index = 1422, .adv_w = 133, .box_w = 8, .box_h = 8, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 1438, .adv_w = 153, .box_w = 9, .box_h = 8, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 1456, .adv_w = 111, .box_w = 7, .box_h = 8, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 1470, .adv_w = 119, .box_w = 7, .box_h = 8, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 1484, .adv_w = 157, .box_w = 9, .box_h = 8, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 1502, .adv_w = 113, .box_w = 7, .box_h = 8, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 1516, .adv_w = 97, .box_w = 6, .box_h = 6, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 1525, .adv_w = 97, .box_w = 6, .box_h = 9, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 1539, .adv_w = 100, .box_w = 6, .box_h = 6, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 1548, .adv_w = 73, .box_w = 5, .box_h = 6, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 1556, .adv_w = 107, .box_w = 7, .box_h = 8, .ofs_x = 0, .ofs_y = -2},
    {.bitmap_index = 1570, .adv_w = 93, .box_w = 6, .box_h = 6, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 1579, .adv_w = 135, .box_w = 9, .box_h = 6, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 1593, .adv_w = 89, .box_w = 6, .box_h = 6, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 1602, .adv_w = 100, .box_w = 6, .box_h = 6, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 1611, .adv_w = 100, .box_w = 6, .box_h = 9, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 1625, .adv_w = 93, .box_w = 6, .box_h = 6, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 1634, .adv_w = 97, .box_w = 6, .box_h = 6, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 1643, .adv_w = 131, .box_w = 8, .box_h = 6, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 1655, .adv_w = 100, .box_w = 6, .box_h = 6, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 1664, .adv_w = 100, .box_w = 6, .box_h = 6, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 1673, .adv_w = 100, .box_w = 6, .box_h = 6, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 1682, .adv_w = 100, .box_w = 6, .box_h = 8, .ofs_x = 0, .ofs_y = -2},
    {.bitmap_index = 1694, .adv_w = 93, .box_w = 6, .box_h = 6, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 1703, .adv_w = 90, .box_w = 6, .box_h = 6, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 1712, .adv_w = 89, .box_w = 6, .box_h = 8, .ofs_x = 0, .ofs_y = -2},
    {.bitmap_index = 1724, .adv_w = 128, .box_w = 8, .box_h = 11, .ofs_x = 0, .ofs_y = -2},
    {.bitmap_index = 1746, .adv_w = 89, .box_w = 6, .box_h = 6, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 1755, .adv_w = 105, .box_w = 7, .box_h = 8, .ofs_x = 0, .ofs_y = -2},
    {.bitmap_index = 1769, .adv_w = 97, .box_w = 6, .box_h = 6, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 1778, .adv_w = 141, .box_w = 9, .box_h = 6, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 1792, .adv_w = 149, .box_w = 9, .box_h = 8, .ofs_x = 0, .ofs_y = -2},
    {.bitmap_index = 1810, .adv_w = 109, .box_w = 7, .box_h = 6, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 1821, .adv_w = 139, .box_w = 8, .box_h = 6, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 1833, .adv_w = 96, .box_w = 6, .box_h = 6, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 1842, .adv_w = 95, .box_w = 6, .box_h = 6, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 1851, .adv_w = 144, .box_w = 9, .box_h = 6, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 1865, .adv_w = 100, .box_w = 6, .box_h = 6, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 1874, .adv_w = 93, .box_w = 6, .box_h = 8, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 1886, .adv_w = 60, .box_w = 3, .box_h = 3, .ofs_x = 0, .ofs_y = 3}
};

/*---------------------
 *  CHARACTER MAPPING
 *--------------------*/

static const uint16_t unicode_list_3[] = {
    0x0, 0x1bd1
};

/*Collect the unicode lists and glyph_id offsets*/
static const lv_font_fmt_txt_cmap_t cmaps[] =
{
    {
        .range_start = 32, .range_length = 95, .glyph_id_start = 1,
        .unicode_list = NULL, .glyph_id_ofs_list = NULL, .list_length = 0, .type = LV_FONT_FMT_TXT_CMAP_FORMAT0_TINY
    },
    {
        .range_start = 176, .range_length = 1, .glyph_id_start = 96,
        .unicode_list = NULL, .glyph_id_ofs_list = NULL, .list_length = 0, .type = LV_FONT_FMT_TXT_CMAP_FORMAT0_TINY
    },
    {
        .range_start = 1040, .range_length = 64, .glyph_id_start = 97,
        .unicode_list = NULL, .glyph_id_ofs_list = NULL, .list_length = 0, .type = LV_FONT_FMT_TXT_CMAP_FORMAT0_TINY
    },
    {
        .range_start = 1105, .range_length = 7122, .glyph_id_start = 161,
        .unicode_list = unicode_list_3, .glyph_id_ofs_list = NULL, .list_length = 2, .type = LV_FONT_FMT_TXT_CMAP_SPARSE_TINY
    }
};

/*-----------------
 *    KERNING
 *----------------*/


/*Map glyph_ids to kern left classes*/
static const uint8_t kern_left_class_mapping[] =
{
    0, 0, 0, 1, 0, 0, 0, 0,
    1, 2, 0, 0, 0, 3, 0, 3,
    0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 3, 0, 0, 0, 0,
    0, 0, 4, 5, 6, 7, 8, 9,
    0, 10, 10, 11, 12, 13, 10, 10,
    7, 14, 15, 16, 0, 17, 11, 18,
    19, 20, 21, 22, 23, 0, 0, 0,
    0, 0, 24, 25, 26, 0, 27, 28,
    0, 29, 0, 0, 30, 0, 29, 29,
    31, 25, 0, 32, 0, 0, 0, 33,
    34, 35, 33, 36, 37, 0, 0, 0,
    0, 4, 38, 5, 39, 40, 8, 20,
    41, 10, 0, 12, 10, 10, 10, 7,
    0, 14, 6, 17, 42, 0, 20, 10,
    0, 10, 43, 44, 0, 44, 45, 46,
    47, 24, 48, 49, 50, 51, 27, 35,
    26, 0, 0, 52, 0, 0, 0, 31,
    0, 25, 26, 53, 33, 25, 35, 54,
    0, 0, 55, 56, 0, 56, 25, 57,
    0, 27, 0
};

/*Map glyph_ids to kern right classes*/
static const uint8_t kern_right_class_mapping[] =
{
    0, 0, 0, 1, 0, 0, 0, 2,
    1, 0, 3, 4, 0, 5, 6, 5,
    0, 0, 0, 0, 0, 0, 0, 0,
    7, 0, 0, 5, 0, 0, 0, 0,
    8, 0, 9, 0, 10, 0, 0, 0,
    10, 0, 0, 11, 0, 0, 0, 0,
    10, 0, 10, 0, 12, 13, 14, 15,
    16, 17, 18, 19, 0, 0, 20, 0,
    0, 0, 21, 0, 22, 22, 22, 23,
    22, 0, 0, 0, 0, 0, 24, 24,
    25, 24, 22, 26, 27, 28, 29, 30,
    31, 32, 30, 33, 0, 0, 34, 0,
    0, 9, 0, 0, 0, 35, 0, 17,
    0, 0, 0, 0, 36, 0, 0, 10,
    0, 0, 10, 13, 37, 0, 17, 0,
    38, 0, 0, 0, 0, 39, 40, 0,
    0, 21, 41, 42, 24, 43, 22, 32,
    44, 24, 24, 24, 45, 24, 24, 25,
    24, 24, 22, 46, 30, 22, 32, 24,
    47, 24, 24, 0, 48, 24, 49, 24,
    50, 22, 51
};

/*Kern values between classes*/
static const int8_t kern_class_values[] =
{
    -3, 0, 0, 0, 0, 0, 0, 0,
    -10, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, -4, -5, 0, -2,
    -8, 0, -8, 0, 0, 0, 1, 0,
    0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0,
    0, 2, 3, 0, 2, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, -24, 0,
    0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0,
    -7, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, -1, 0, 0, -3,
    0, 0, 0, 2, 0, 0, 0, -5,
    0, 0, 0, 0, 0, 0, -5, 0,
    0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0,
    -2, 0, -2, 0, 0, -5, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0,
    -1, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0,
    0, -2, 0, 0, 0, 0, 0, 0,
    0, 0, 0, -2, 0, 0, 0, 0,
    0, 0, -1, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0,
    -1, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, -10, 0,
    0, 0, -2, 0, 0, 0, -2, 0,
    -2, 0, -2, -4, -2, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, -3, -3, 0, 0,
    -3, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0,
    0, 2, 0, 0, 0, 0, 0, 0,
    0, 0, -2, -2, 0, -2, 0, 0,
    0, -1, -2, -2, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, -23, 0, 0, 0,
    -5, 0, 0, 0, 2, 0, 0, 0,
    0, 0, 0, 0, -3, -2, 0, 0,
    -2, -2, 0, 0, -2, -2, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 2, 0, 0, 0, -2,
    0, 0, 0, 1, -2, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 2, 1, 0,
    -2, 0, 0, 0, 0, 2, 0, 1,
    0, -2, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, -2, 0,
    0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, -14, 0,
    0, 0, -3, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, -2, 0,
    0, -2, 0, 0, 0, -2, -3, -5,
    0, 0, 0, 0, 0, 0, 0, 0,
    0, -4, 0, 0, 0, 0, -6, -8,
    0, 0, 0, 0, -22, 0, 0, 0,
    0, 0, 0, 0, 2, -2, 0, 0,
    -18, -2, -18, -8, 0, -24, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0,
    -1, -11, -4, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, -35, 0, 0, 0, -15,
    0, 0, 0, 0, 0, 0, 0, -4,
    0, -3, 0, -1, -1, 0, 0, -1,
    0, 0, 1, 0, 1, 0, 0, 0,
    0, -20, -12, 0, 0, 0, 0, 0,
    0, -15, 0, -8, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, -3, 0,
    -2, -2, 0, -3, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0,
    0, -4, 0, -2, 0, 0, -4, 0,
    0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, -22, -23, 0, 0,
    -9, -2, 0, -1, 1, 0, 1, 1,
    0, 1, 0, 0, -7, -8, 0, -8,
    -7, -6, -7, 0, -6, -7, -4, -7,
    -5, 0, -7, -5, 0, 0, 1, 0,
    -2, -8, -12, -11, -11, -7, -11, -11,
    -11, -12, 0, 0, 0, 2, 0, -18,
    -13, 0, 0, -6, -1, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 1, -4,
    -4, 0, 0, -4, -3, 0, 0, -2,
    -1, 0, 0, 0, 2, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0,
    1, 0, -12, -5, 0, 0, -4, 0,
    0, 0, 1, 0, 0, 0, 0, 0,
    0, 1, -3, -3, 0, 0, -3, -2,
    0, 0, -2, 0, 0, 0, 0, 1,
    0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, -13, 0,
    0, 0, -2, 0, 0, 0, 0, 1,
    0, 0, 0, 0, 0, 0, -2, 0,
    0, -2, 0, 0, 0, -2, -3, 0,
    0, 0, 0, 2, 1, 0, 0, 0,
    0, -2, 0, 0, 0, 1, -4, -4,
    0, 0, 0, 0, 0, -3, 2, -4,
    -20, -13, 0, 0, -13, -2, -8, -1,
    1, -8, 2, 1, 1, 2, 0, 2,
    -5, -6, -2, -3, -6, -3, -5, -2,
    -3, -2, 0, -2, -3, 2, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, -4, 0,
    0, 0, 0, 0, 0, 0, 0, 1,
    -2, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, -2, 0, 0, -2,
    0, 0, 0, -2, -2, -2, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, -2, 0, 0, -2,
    0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, -1, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, -1, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, -1, 0, 0, 0, 0, 0,
    -2, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, -1, 0, -1,
    -1, 0, 0, 0, 0, 0, 0, 0,
    0, 0, -2, 0, -1, -2, 0, 0,
    0, 0, 0, -1, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, -1, 0,
    0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, -1, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, -1, 0, 0, 0, 0,
    0, 1, 0, 2, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 2, 0, -2, 0,
    0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 2, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, -1, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, -2, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, -2, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, -1,
    0, -2, -1, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, -1, -2,
    0, 0, 0, 0, 0, 1, 0, 0,
    0, -15, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, -2, 1, 0, -2, 0, 0,
    1, 0, 2, 1, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0,
    1, 0, 0, 0, -14, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, -1, -1, 1, 0,
    -1, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, -5, 0, -4, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, -11,
    0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, -2, 0, 0, -3, 0,
    0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, -1, 0,
    0, -1, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, -2, 0,
    0, -2, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, -2, 0, -2, 0, -1,
    -2, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, -1, 0,
    0, 0, 0, -2, -2, -2, 0, 0,
    0, 0, 0, 0, -4, 0, 0, 0,
    0, 0, 0, 0, 0, 0, -39, -41,
    0, 0, -18, -6, 0, -4, 1, 0,
    1, 0, 0, 1, 0, 0, -14, -15,
    0, -15, -13, -11, -13, 0, -11, -14,
    -9, -13, -10, 0, -13, -9, 0, 0,
    0, 0, -6, -16, -22, -20, -21, -13,
    -21, -21, -20, -21, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 1, -2, 0,
    0, -3, 0, -3, 0, 0, -3, 0,
    0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 2,
    1, 0, -3, -3, 0, 0, 0, 2,
    0, 1, 0, -3, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, -1, 0,
    0, 0, 0, 0, -1, 0, -1, 0,
    -1, -1, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, -1, -1, 0, -1, 0,
    0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, -34,
    -17, 0, 0, -8, -2, 0, 0, 2,
    0, 0, 0, 0, 2, 0, 0, -7,
    -6, 0, -7, -6, 0, -4, 0, 0,
    0, 0, 0, 0, 0, -7, -5, 0,
    0, 2, 0, 0, -3, -7, -4, -6,
    0, -2, -3, 0, -4, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 1, 0,
    0, 0, -2, 0, -2, 0, 1, -2,
    0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0,
    2, 1, 1, -2, -2, -1, 0, 0,
    2, 0, 1, -2, -2, 0, 0, 0,
    0, -5, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, -9, 0, -5,
    0, 0, -7, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0,
    -5, 0, 0, 0, 0, -2, -2, -5,
    0, 0, 0, 0, 0, 0, -4, 0,
    0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0,
    -2, 0, 0, 0, -2, -2, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, -3, -2,
    -2, 0, 0, 0, 0, 0, 0, 0,
    -2, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, -3, 0, 0, 0, -2,
    0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0,
    0, -3, -2, -2, 0, 0, 0, 0,
    0, -2, 0, -2, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, -2, 0,
    0, 0, 0, -1, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, -1, 0, 0, 0,
    0, 0, 0, 0, -1, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, -2,
    0, 0, -2, 0, 0, 0, 0, 0,
    -1, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, -1, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, -1, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0,
    -2, 0, 0, -2, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, -5, 0, -3,
    0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0,
    2, 0, 0, -2, -2, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, -1, 0,
    0, -5, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 1, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0,
    -1, -2, 1, 0, -2, 0, 0, 0,
    0, 1, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, -5, 0,
    -4, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0,
    0, 2, 0, 1, -1, -1, 0, 0,
    1, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, -2,
    0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 2, 0, 1, -2,
    -2, 0, 0, 0, 0, -12, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, -3, 0, -1, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, -6, -2, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, -1, 0, -1,
    0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, -1, 0, -1, 0, 0, 0,
    0, 0, 0
};


/*Collect the kern class' data in one place*/
static const lv_font_fmt_txt_kern_classes_t kern_classes =
{
    .class_pair_values   = kern_class_values,
    .left_class_mapping  = kern_left_class_mapping,
    .right_class_mapping = kern_right_class_mapping,
    .left_class_cnt      = 57,
    .right_class_cnt     = 51,
};

/*--------------------
 *  ALL CUSTOM DATA
 *--------------------*/

#if LVGL_VERSION_MAJOR == 8
/*Store all the custom data of the font*/
static  lv_font_fmt_txt_glyph_cache_t cache;
#endif

#if LVGL_VERSION_MAJOR >= 8
static const lv_font_fmt_txt_dsc_t font_dsc = {
#else
static lv_font_fmt_txt_dsc_t font_dsc = {
#endif
    .glyph_bitmap = glyph_bitmap,
    .glyph_dsc = glyph_dsc,
    .cmaps = cmaps,
    .kern_dsc = &kern_classes,
    .kern_scale = 16,
    .cmap_num = 4,
    .bpp = 2,
    .kern_classes = 1,
    .bitmap_format = 0,
#if LVGL_VERSION_MAJOR == 8
    .cache = &cache
#endif
};



/*-----------------
 *  PUBLIC FONT
 *----------------*/

/*Initialize a public general font descriptor*/
#if LVGL_VERSION_MAJOR >= 8
const lv_font_t ui_font_number11 = {
#else
lv_font_t ui_font_number11 = {
#endif
    .get_glyph_dsc = lv_font_get_glyph_dsc_fmt_txt,    /*Function pointer to get glyph's data*/
    .get_glyph_bitmap = lv_font_get_bitmap_fmt_txt,    /*Function pointer to get glyph's bitmap*/
    .line_height = 13,          /*The maximum line height required by the font*/
    .base_line = 3,             /*Baseline measured from the bottom of the line*/
#if !(LVGL_VERSION_MAJOR == 6 && LVGL_VERSION_MINOR == 0)
    .subpx = LV_FONT_SUBPX_NONE,
#endif
#if LV_VERSION_CHECK(7, 4, 0) || LVGL_VERSION_MAJOR >= 8
    .underline_position = -1,
    .underline_thickness = 1,
#endif
    .dsc = &font_dsc,          /*The custom font data. Will be accessed by `get_glyph_bitmap/dsc` */
#if LV_VERSION_CHECK(8, 2, 0) || LVGL_VERSION_MAJOR >= 9
    .fallback = NULL,
#endif
    .user_data = NULL,
};



#endif /*#if UI_FONT_NUMBER11*/

