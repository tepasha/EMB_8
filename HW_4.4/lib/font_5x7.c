#include "font_5x7.h"
#include <stddef.h>

// Кожен рядок - 5 байт (стовпці), біт0 = верхній піксель, ..., біт6 = нижній.
static const uint8_t GLYPH_SPACE[5] = {0x00, 0x00, 0x00, 0x00, 0x00};
static const uint8_t GLYPH_DOT[5]   = {0x00, 0x00, 0x40, 0x00, 0x00};
static const uint8_t GLYPH_COLON[5] = {0x00, 0x14, 0x00, 0x00, 0x00};
static const uint8_t GLYPH_MINUS[5] = {0x00, 0x08, 0x08, 0x08, 0x00};
static const uint8_t GLYPH_PCT[5]   = {0x62, 0x64, 0x08, 0x16, 0x26};
static const uint8_t GLYPH_DEG[5]   = {0x00, 0x06, 0x09, 0x09, 0x06};

static const uint8_t GLYPH_0[5] = {0x3E, 0x51, 0x49, 0x45, 0x3E};
static const uint8_t GLYPH_1[5] = {0x00, 0x42, 0x7F, 0x40, 0x00};
static const uint8_t GLYPH_2[5] = {0x62, 0x51, 0x49, 0x49, 0x46};
static const uint8_t GLYPH_3[5] = {0x22, 0x41, 0x49, 0x49, 0x36};
static const uint8_t GLYPH_4[5] = {0x18, 0x14, 0x12, 0x7F, 0x10};
static const uint8_t GLYPH_5[5] = {0x27, 0x45, 0x45, 0x45, 0x39};
static const uint8_t GLYPH_6[5] = {0x3C, 0x4A, 0x49, 0x49, 0x30};
static const uint8_t GLYPH_7[5] = {0x01, 0x71, 0x09, 0x05, 0x03};
static const uint8_t GLYPH_8[5] = {0x36, 0x49, 0x49, 0x49, 0x36};
static const uint8_t GLYPH_9[5] = {0x06, 0x49, 0x49, 0x29, 0x1E};

static const uint8_t GLYPH_B[5] = {0x7F, 0x49, 0x49, 0x49, 0x36};
static const uint8_t GLYPH_C[5] = {0x3E, 0x41, 0x41, 0x41, 0x22};
static const uint8_t GLYPH_D[5] = {0x7F, 0x41, 0x41, 0x22, 0x1C};
static const uint8_t GLYPH_H[5] = {0x7F, 0x08, 0x08, 0x08, 0x7F};
static const uint8_t GLYPH_P[5] = {0x7F, 0x09, 0x09, 0x09, 0x06};
static const uint8_t GLYPH_R[5] = {0x7F, 0x09, 0x19, 0x29, 0x46};
static const uint8_t GLYPH_S[5] = {0x26, 0x49, 0x49, 0x49, 0x32};
static const uint8_t GLYPH_T[5] = {0x01, 0x01, 0x7F, 0x01, 0x01};

static const uint8_t GLYPH_a[5] = {0x20, 0x54, 0x54, 0x54, 0x78};
static const uint8_t GLYPH_h[5] = {0x7F, 0x08, 0x04, 0x04, 0x78};

const uint8_t *font5x7_get_glyph(char c)
{
    switch (c) {
        case ' ': return GLYPH_SPACE;
        case '.': return GLYPH_DOT;
        case ':': return GLYPH_COLON;
        case '-': return GLYPH_MINUS;
        case '%': return GLYPH_PCT;
        case (char)0xB0: return GLYPH_DEG; // символ градуса

        case '0': return GLYPH_0;
        case '1': return GLYPH_1;
        case '2': return GLYPH_2;
        case '3': return GLYPH_3;
        case '4': return GLYPH_4;
        case '5': return GLYPH_5;
        case '6': return GLYPH_6;
        case '7': return GLYPH_7;
        case '8': return GLYPH_8;
        case '9': return GLYPH_9;

        case 'B': return GLYPH_B;
        case 'C': return GLYPH_C;
        case 'D': return GLYPH_D;
        case 'H': return GLYPH_H;
        case 'P': return GLYPH_P;
        case 'R': return GLYPH_R;
        case 'S': return GLYPH_S;
        case 'T': return GLYPH_T;

        case 'a': return GLYPH_a;
        case 'h': return GLYPH_h;

        default: return NULL; // непідтримуваний символ -> буде намальований пробіл
    }
}
